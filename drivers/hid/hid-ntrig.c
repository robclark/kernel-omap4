/*
 *  HID driver for N-Trig touchscreens
 *
 *  Copyright (c) 2008-2010 Rafi Rubin
 *  Copyright (c) 2009-2010 Stephane Chatty
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include "usbhid/usbhid.h"
#include <linux/module.h>
#include <linux/slab.h>

#include "hid-ids.h"

#define NTRIG_DUPLICATE_USAGES	0x001

#define MAX_EVENTS		120

#define SN_MOVE_X		128
#define SN_MOVE_Y		92
#define SN_MAJOR		48

static unsigned int min_width;
static unsigned int min_height;

static unsigned int activate_slack = 1;

static unsigned int deactivate_slack = 4;

static unsigned int activation_width = 64;
static unsigned int activation_height = 32;

struct ntrig_data {
	/* Incoming raw values for a single contact */
	__u16 x, y, w, h;
	__u16 id;

	bool tipswitch;
	bool confidence;
	bool first_contact_touch;

	bool reading_mt;

	__u8 mt_footer[4];
	__u8 mt_foot_count;

	/* The current activation state. */
	__s8 act_state;

	/* Empty frames to ignore before recognizing the end of activity */
	__s8 deactivate_slack;

	/* Frames to ignore before acknowledging the start of activity */
	__s8 activate_slack;

	/* Minimum size contact to accept */
	__u16 min_width;
	__u16 min_height;

	/* Threshold to override activation slack */
	__u16 activation_width;
	__u16 activation_height;

	__u16 sensor_logical_width;
	__u16 sensor_logical_height;
	__u16 sensor_physical_width;
	__u16 sensor_physical_height;
};


/*
 * This function converts the 4 byte raw firmware code into
 * a string containing 5 comma separated numbers.
 */
static int ntrig_version_string(unsigned char *raw, char *buf)
{
	__u8 a =  (raw[1] & 0x0e) >> 1;
	__u8 b =  (raw[0] & 0x3c) >> 2;
	__u8 c = ((raw[0] & 0x03) << 3) | ((raw[3] & 0xe0) >> 5);
	__u8 d = ((raw[3] & 0x07) << 3) | ((raw[2] & 0xe0) >> 5);
	__u8 e =   raw[2] & 0x07;

	/*
	 * As yet unmapped bits:
	 * 0b11000000 0b11110001 0b00011000 0b00011000
	 */

	return sprintf(buf, "%u.%u.%u.%u.%u", a, b, c, d, e);
}

static void ntrig_report_version(struct hid_device *hdev)
{
	int ret;
	char buf[20];
	struct usb_device *usb_dev = hid_to_usb_dev(hdev);
	unsigned char *data = kmalloc(8, GFP_KERNEL);

	if (!data)
		goto err_free;

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      USB_REQ_CLEAR_FEATURE,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE |
			      USB_DIR_IN,
			      0x30c, 1, data, 8,
			      USB_CTRL_SET_TIMEOUT);

	if (ret == 8) {
		ret = ntrig_version_string(&data[2], buf);

		hid_info(hdev, "Firmware version: %s (%02x%02x %02x%02x)\n",
			 buf, data[2], data[3], data[4], data[5]);
	}

err_free:
	kfree(data);
}

/*
 * this driver is aimed at two firmware versions in circulation:
 *  - dual pen/finger single touch
 *  - finger multitouch, pen not working
 */

static int ntrig_input_mapping(struct hid_device *hdev, struct hid_input *hi,
			       struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct ntrig_data *nd = hid_get_drvdata(hdev);
	struct input_dev *input = hi->input;
	int f1 = field->logical_minimum;
	int f2 = field->logical_maximum;
	int df = f2 - f1;

	/* No special mappings needed for the pen and single touch */
	if (field->physical)
		return 0;

	switch (usage->hid & HID_USAGE_PAGE) {
	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max, EV_ABS, ABS_X);
			input_set_abs_params(input, ABS_X,
					     f1, f2, df / SN_MOVE_X, 0);
			input_set_abs_params(input, ABS_MT_POSITION_X,
					     f1, f2, df / SN_MOVE_X, 0);
			if (!nd->sensor_logical_width) {
				nd->sensor_logical_width =
					field->logical_maximum -
					field->logical_minimum;
				nd->sensor_physical_width =
					field->physical_maximum -
					field->physical_minimum;
				nd->activation_width = activation_width *
					nd->sensor_logical_width /
					nd->sensor_physical_width;
				nd->min_width = min_width *
					nd->sensor_logical_width /
					nd->sensor_physical_width;
			}
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max, EV_ABS, ABS_Y);
			input_set_abs_params(input, ABS_Y,
					     f1, f2, df / SN_MOVE_Y, 0);
			input_set_abs_params(input, ABS_MT_POSITION_Y,
					     f1, f2, df / SN_MOVE_Y, 0);
			if (!nd->sensor_logical_height) {
				nd->sensor_logical_height =
					field->logical_maximum -
					field->logical_minimum;
				nd->sensor_physical_height =
					field->physical_maximum -
					field->physical_minimum;
				nd->activation_height = activation_height *
					nd->sensor_logical_height /
					nd->sensor_physical_height;
				nd->min_height = min_height *
					nd->sensor_logical_height /
					nd->sensor_physical_height;
			}
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		/* we do not want to map these for now */
		case HID_DG_CONTACTID: /* Not trustworthy, squelch for now */
		case HID_DG_INPUTMODE:
		case HID_DG_DEVICEINDEX:
		case HID_DG_CONTACTMAX:
		case HID_DG_CONTACTCOUNT:
		case HID_DG_INRANGE:
		case HID_DG_CONFIDENCE:
			return -1;

		case HID_DG_TIPSWITCH:
			hid_map_usage(hi, usage, bit, max, EV_KEY, BTN_TOUCH);
			input_set_capability(input, EV_KEY, BTN_TOUCH);
			return 1;

		/* width/height mapped on TouchMajor/TouchMinor/Orientation */
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MAJOR);
			input_set_abs_params(input, ABS_MT_TOUCH_MAJOR,
					     f1, f2, df / SN_MAJOR, 0);
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_TOUCH_MINOR);
			input_set_abs_params(input, ABS_MT_TOUCH_MINOR,
					     f1, f2, df / SN_MAJOR, 0);
			input_set_abs_params(input, ABS_MT_ORIENTATION,
					     0, 1, 0, 0);
			input_set_events_per_packet(input, MAX_EVENTS);
			return 1;
		}
		return 0;
	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int ntrig_input_mapped(struct hid_device *hdev, struct hid_input *hi,
			      struct hid_field *field, struct hid_usage *usage,
			      unsigned long **bit, int *max)
{
	/* No special mappings needed for the pen and single touch */
	if (field->physical)
		return 0;

	/* tell hid-input to skip setup of these event types */
	if (usage->type == EV_KEY || usage->type == EV_ABS)
		set_bit(usage->type, hi->input->evbit);
	return -1;
}

/*
 * this function is called upon all reports
 * so that we can filter contact point information,
 * decide whether we are in multi or single touch mode
 * and call input_mt_sync after each point if necessary
 */
static int ntrig_event (struct hid_device *hid, struct hid_field *field,
			struct hid_usage *usage, __s32 value)
{
	struct input_dev *input = field->hidinput->input;
	struct ntrig_data *nd = hid_get_drvdata(hid);

	/* No special handling needed for the pen */
	if (field->application == HID_DG_PEN)
		return 0;

        if (hid->claimed & HID_CLAIMED_INPUT) {
		switch (usage->hid) {
		case 0xff000001:
			/* Tag indicating the start of a multitouch group */
			nd->reading_mt = 1;
			nd->first_contact_touch = 0;
			break;
		case HID_DG_TIPSWITCH:
			nd->tipswitch = value;
			/* Prevent emission of touch until validated */
			return 1;
		case HID_DG_CONFIDENCE:
			nd->confidence = value;
			break;
		case HID_GD_X:
			nd->x = value;
			/* Clear the contact footer */
			nd->mt_foot_count = 0;
			break;
		case HID_GD_Y:
			nd->y = value;
			break;
		case HID_DG_CONTACTID:
			nd->id = value;
			break;
		case HID_DG_WIDTH:
			nd->w = value;
			break;
		case HID_DG_HEIGHT:
			nd->h = value;
			/*
			 * when in single touch mode, this is the last
			 * report received in a finger event. We want
			 * to emit a normal (X, Y) position
			 */
			if (!nd->reading_mt) {
				/*
				 * TipSwitch indicates the presence of a
				 * finger in single touch mode.
				 */
				input_report_key(input, BTN_TOUCH,
						 nd->tipswitch);
				input_report_key(input, BTN_TOOL_DOUBLETAP,
						 nd->tipswitch);
				input_event(input, EV_ABS, ABS_X, nd->x);
				input_event(input, EV_ABS, ABS_Y, nd->y);
			}
			break;
		case 0xff000002:
			/*
			 * we receive this when the device is in multitouch
			 * mode. The first of the three values tagged with
			 * this usage tells if the contact point is real
			 * or a placeholder
			 */

			/* Shouldn't get more than 4 footer packets, so skip */
			if (nd->mt_foot_count >= 4)
				break;

			nd->mt_footer[nd->mt_foot_count++] = value;

			/* if the footer isn't complete break */
			if (nd->mt_foot_count != 4)
				break;

			/* Pen activity signal. */
			if (nd->mt_footer[2]) {
				/*
				 * When the pen deactivates touch, we see a
				 * bogus frame with ContactCount > 0.
				 * We can
				 * save a bit of work by ensuring act_state < 0
				 * even if deactivation slack is turned off.
				 */
				nd->act_state = deactivate_slack - 1;
				nd->confidence = 0;
				break;
			}

			/*
			 * The first footer value indicates the presence of a
			 * finger.
			 */
			if (nd->mt_footer[0]) {
				/*
				 * We do not want to process contacts under
				 * the size threshold, but do not want to
				 * ignore them for activation state
				 */
				if (nd->w < nd->min_width ||
				    nd->h < nd->min_height)
					nd->confidence = 0;
			} else
				break;

			if (nd->act_state > 0) {
				/*
				 * Contact meets the activation size threshold
				 */
				if (nd->w >= nd->activation_width &&
				    nd->h >= nd->activation_height) {
					if (nd->id)
						/*
						 * first contact, activate now
						 */
						nd->act_state = 0;
					else {
						/*
						 * avoid corrupting this frame
						 * but ensure next frame will
						 * be active
						 */
						nd->act_state = 1;
						break;
					}
				} else
					/*
					 * Defer adjusting the activation state
					 * until the end of the frame.
					 */
					break;
			}

			/* Discarding this contact */
			if (!nd->confidence)
				break;

			/* emit a normal (X, Y) for the first point only */
			if (nd->id == 0) {
				/*
				 * TipSwitch is superfluous in multitouch
				 * mode.  The footer events tell us
				 * if there is a finger on the screen or
				 * not.
				 */
				nd->first_contact_touch = nd->confidence;
				input_event(input, EV_ABS, ABS_X, nd->x);
				input_event(input, EV_ABS, ABS_Y, nd->y);
			}

			/* Emit MT events */
			input_event(input, EV_ABS, ABS_MT_POSITION_X, nd->x);
			input_event(input, EV_ABS, ABS_MT_POSITION_Y, nd->y);

			/*
			 * Translate from height and width to size
			 * and orientation.
			 */
			if (nd->w > nd->h) {
				input_event(input, EV_ABS,
						ABS_MT_ORIENTATION, 1);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MAJOR, nd->w);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MINOR, nd->h);
			} else {
				input_event(input, EV_ABS,
						ABS_MT_ORIENTATION, 0);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MAJOR, nd->h);
				input_event(input, EV_ABS,
						ABS_MT_TOUCH_MINOR, nd->w);
			}
			input_mt_sync(field->hidinput->input);
			break;

		case HID_DG_CONTACTCOUNT: /* End of a multitouch group */
			if (!nd->reading_mt) /* Just to be sure */
				break;

			nd->reading_mt = 0;


			/*
			 * Activation state machine logic:
			 *
			 * Fundamental states:
			 *	state >  0: Inactive
			 *	state <= 0: Active
			 *	state <  -deactivate_slack:
			 *		 Pen termination of touch
			 *
			 * Specific values of interest
			 *	state == activate_slack
			 *		 no valid input since the last reset
			 *
			 *	state == 0
			 *		 general operational state
			 *
			 *	state == -deactivate_slack
			 *		 read sufficient empty frames to accept
			 *		 the end of input and reset
			 */

			if (nd->act_state > 0) { /* Currently inactive */
				if (value)
					/*
					 * Consider each live contact as
					 * evidence of intentional activity.
					 */
					nd->act_state = (nd->act_state > value)
							? nd->act_state - value
							: 0;
				else
					/*
					 * Empty frame before we hit the
					 * activity threshold, reset.
					 */
					nd->act_state = nd->activate_slack;

				/*
				 * Entered this block inactive and no
				 * coordinates sent this frame, so hold off
				 * on button state.
				 */
				break;
			} else { /* Currently active */
				if (value && nd->act_state >=
					     nd->deactivate_slack)
					/*
					 * Live point: clear accumulated
					 * deactivation count.
					 */
					nd->act_state = 0;
				else if (nd->act_state <= nd->deactivate_slack)
					/*
					 * We've consumed the deactivation
					 * slack, time to deactivate and reset.
					 */
					nd->act_state =
						nd->activate_slack;
				else { /* Move towards deactivation */
					nd->act_state--;
					break;
				}
			}

			if (nd->first_contact_touch && nd->act_state <= 0) {
				/*
				 * Check to see if we're ready to start
				 * emitting touch events.
				 *
				 * Note: activation slack will decrease over
				 * the course of the frame, and it will be
				 * inconsistent from the start to the end of
				 * the frame.  However if the frame starts
				 * with slack, first_contact_touch will still
				 * be 0 and we will not get to this point.
				 */
				input_report_key(input, BTN_TOOL_DOUBLETAP, 1);
				input_report_key(input, BTN_TOUCH, 1);
			} else {
				input_report_key(input, BTN_TOOL_DOUBLETAP, 0);
				input_report_key(input, BTN_TOUCH, 0);
			}
			break;

		default:
			/* fall-back to the generic hidinput handling */
			return 0;
		}
	}

	/* we have handled the hidinput part, now remains hiddev */
	if ((hid->claimed & HID_CLAIMED_HIDDEV) && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int ntrig_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct ntrig_data *nd;
	struct hid_input *hidinput;
	struct input_dev *input;
	struct hid_report *report;

	if (id->driver_data & NTRIG_DUPLICATE_USAGES)
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;

	nd = kzalloc(sizeof(struct ntrig_data), GFP_KERNEL);
	if (!nd) {
		hid_err(hdev, "cannot allocate N-Trig data\n");
		return -ENOMEM;
	}

	nd->activate_slack = activate_slack;
	nd->act_state = activate_slack;
	nd->deactivate_slack = -deactivate_slack;
	hid_set_drvdata(hdev, nd);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}


	list_for_each_entry(hidinput, &hdev->inputs, list) {
		if (hidinput->report->maxfield < 1)
			continue;

		input = hidinput->input;
		switch (hidinput->report->field[0]->application) {
		case HID_DG_PEN:
			input->name = "N-Trig Pen";
			break;
		case HID_DG_TOUCHSCREEN:
			input->name =
				(hidinput->report->field[0]
				 ->physical) ?
				"N-Trig Touchscreen" :
				"N-Trig MultiTouch";
			break;
		}
	}

	/* This is needed for devices with more recent firmware versions */
	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[0x0a];
	if (report)
		usbhid_submit_report(hdev, report, USB_DIR_OUT);

	ntrig_report_version(hdev);

	return 0;
err_free:
	kfree(nd);
	return ret;
}

static void ntrig_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
}
#define NTRIG_DEVICE(id)						\
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, id),			\
			.driver_data = NTRIG_DUPLICATE_USAGES  }

static const struct hid_device_id ntrig_devices[] = {
	NTRIG_DEVICE(0x0001),
	{ }
};
MODULE_DEVICE_TABLE(hid, ntrig_devices);

static const struct hid_usage_id ntrig_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1 }
};

static struct hid_driver ntrig_driver = {
	.name = "ntrig",
	.id_table = ntrig_devices,
	.probe = ntrig_probe,
	.remove = ntrig_remove,
	.input_mapping = ntrig_input_mapping,
	.input_mapped = ntrig_input_mapped,
	.usage_table = ntrig_grabbed_usages,
	.event = ntrig_event,
};

static int __init ntrig_init(void)
{
	return hid_register_driver(&ntrig_driver);
}

static void __exit ntrig_exit(void)
{
	hid_unregister_driver(&ntrig_driver);
}

module_init(ntrig_init);
module_exit(ntrig_exit);
MODULE_LICENSE("GPL");
