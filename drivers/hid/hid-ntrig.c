/*
 *  HID driver for N-Trig touchscreens
 *
 *  Copyright (c) 2008-2010 Rafi Rubin
 *  Copyright (c) 2009-2010 Stephane Chatty
 *  Copyright (c) 2010 Canonical, Ltd.
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

#define MAX_SLOTS		20
#define MAX_EVENTS		120

#define SN_MOVE_X		128
#define SN_MOVE_Y		92
#define SN_MAJOR		48

#define HOLD_MIN		3
#define HOLD_MED		7
#define HOLD_MAX		10

#define DIV_MIN			8
#define DIV_MED			40
#define DIV_MAX			100

struct ntrig_contact {
	int x, y, w, h;
};

struct ntrig_data {
	struct ntrig_contact row[MAX_SLOTS], col[MAX_SLOTS];
	int dmin, dmed, dmax;
	int nrow, ncol;
	int index, nindex;
	int nhold;
	bool touch;
	bool hasmt;
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
			nd->dmin = df / DIV_MIN;
			nd->dmed = df / DIV_MED;
			nd->dmax = df / DIV_MAX;
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max, EV_ABS, ABS_Y);
			input_set_abs_params(input, ABS_Y,
					     f1, f2, df / SN_MOVE_Y, 0);
			input_set_abs_params(input, ABS_MT_POSITION_Y,
					     f1, f2, df / SN_MOVE_Y, 0);
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
		switch (usage->hid) {
		case 0xff000001:
			/* multi-touch firmware */
			nd->hasmt = true;
			break;
		}
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

static bool copy_best_column(struct ntrig_data *nd, unsigned *used, int i)
{
	int j, jbest = -1, d, dbest;
	for (j = 0; j < nd->ncol; ++j) {
		if (*used & (1 << j))
			continue;
		d = abs(nd->row[i].x - nd->col[j].x) +
			abs(nd->row[i].y - nd->col[j].y);
		if (jbest < 0 || d < dbest) {
			jbest = j;
			dbest = d;
		}
	}
	if (jbest < 0)
		return false;
	*used |= (1 << jbest);
	if (nd->nrow > 2)
		nd->col[jbest].y = (nd->row[i].y + nd->col[jbest].y) >> 1;
	nd->row[i] = nd->col[jbest];
	return true;
}

static bool copy_next_column(struct ntrig_data *nd, unsigned *used, int i)
{
	int j;
	for (j = 0; j < nd->ncol; ++j) {
		if (*used & (1 << j))
			continue;
		*used |= (1 << j);
		nd->row[i] = nd->col[j];
		return true;
	}
	return false;
}

static int ghost_distance(const struct ntrig_data *nd, int j)
{
	int i, d, dbest = INT_MAX;
	for (i = 0; i < nd->nrow; ++i) {
		d = abs(nd->row[i].x - nd->col[j].x);
		dbest = min(dbest, d);
		d = abs(nd->row[i].y - nd->col[j].y);
		dbest = min(dbest, d);
	}
	return dbest;
}

static void discard_ghosts(struct ntrig_data *nd, unsigned *used)
{
	int j, d;
	for (j = 0; j < nd->ncol; ++j) {
		if (*used & (1 << j))
			continue;
		d = ghost_distance(nd, j);
		if ((nd->nhold < HOLD_MIN && d < nd->dmin) ||
		    (nd->nhold < HOLD_MED && d < nd->dmed) ||
		    (nd->nhold < HOLD_MAX && d < nd->dmax))
			*used |= (1 << j);
	}
}

static void report_frame(struct input_dev *input, struct ntrig_data *nd)
{
	struct ntrig_contact *oldest = 0;
	unsigned used = 0;
	int i;

	if (nd->nrow < nd->ncol) {
		for (i = 0; i < nd->nrow; ++i)
			copy_best_column(nd, &used, i);
		if (nd->ncol > 2)
			discard_ghosts(nd, &used);
		while (copy_next_column(nd, &used, i))
			i++;
		nd->nrow = i;
		nd->nhold++;
	} else if (nd->nrow > nd->ncol) {
		for (i = 0; i < nd->ncol; ++i)
			nd->row[i] = nd->col[i];
		nd->nrow = nd->ncol;
		nd->nhold = 0;
	} else {
		unsigned used = 0;
		for (i = 0; i < nd->nrow; ++i)
			copy_best_column(nd, &used, i);
		nd->nhold = 0;
	}

	for (i = 0; i < nd->nrow; ++i) {
		struct ntrig_contact *f = &nd->row[i];
		int wide = (f->w > f->h);
		int major = max(f->w, f->h);
		int minor = min(f->w, f->h);
		if (!oldest)
			oldest = f;
		input_event(input, EV_ABS, ABS_MT_POSITION_X, f->x);
		input_event(input, EV_ABS, ABS_MT_POSITION_Y, f->y);
		input_event(input, EV_ABS, ABS_MT_ORIENTATION, wide);
		input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
		input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, minor);
		input_mt_sync(input);
	}

	/* touchscreen emulation */
	if (oldest) {
		input_event(input, EV_KEY, BTN_TOUCH, 1);
		input_event(input, EV_ABS, ABS_X, oldest->x);
		input_event(input, EV_ABS, ABS_Y, oldest->y);
	} else {
		input_event(input, EV_KEY, BTN_TOUCH, 0);
	}
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
		case HID_DG_TIPSWITCH:
			nd->touch = value;
			if (nd->nindex < MAX_SLOTS)
				nd->index = nd->nindex++;
			break;
		case HID_GD_X:
			nd->col[nd->index].x = value;
			break;
		case HID_GD_Y:
			nd->col[nd->index].y = value;
			break;
		case HID_DG_WIDTH:
			nd->col[nd->index].w = value;
			break;
		case HID_DG_HEIGHT:
			nd->col[nd->index].h = value;
			if (!nd->hasmt) {
				nd->nindex = 0;
				nd->ncol = nd->touch;
				report_frame(input, nd);
			}
			break;
		case HID_DG_CONTACTCOUNT: /* End of a multitouch group */
			if (!nd->hasmt)
				break;
			nd->nindex = 0;
			nd->ncol = value;
			/* skip pen switch events */
			if (nd->ncol == 1 &&
			    nd->col[0].w == 10 && nd->col[0].h == 10)
				break;
			report_frame(input, nd);
			break;
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
