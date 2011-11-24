/*
 * OmniVision OV5650 sensor driver
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#include <media/ov5650.h>

/* OV5650 has only one fixed colorspace per pixelcode */
struct ov5650_datafmt {
	enum v4l2_mbus_pixelcode	code;
	enum v4l2_colorspace		colorspace;
};

enum ov5650_size {
	OV5650_SIZE_5MP,
	OV5650_SIZE_LAST,
};

static const struct v4l2_frmsize_discrete ov5650_frmsizes[OV5650_SIZE_LAST] = {
	{ 2592, 1944 },
};

/* Find a frame size in an array */
static int ov5650_find_framesize(u32 width, u32 height)
{
	int i;

	for (i = 0; i < OV5650_SIZE_LAST; i++) {
		if ((ov5650_frmsizes[i].width >= width) &&
		    (ov5650_frmsizes[i].height >= height))
			break;
	}

	/* If not found, select biggest */
	if (i >= OV5650_SIZE_LAST)
		i = OV5650_SIZE_LAST - 1;

	return i;
}

struct ov5650 {
	struct v4l2_subdev subdev;
	struct media_pad pad;

	struct v4l2_mbus_framefmt format;

	struct v4l2_ctrl_handler ctrl_handler;

	const struct ov5650_platform_data *pdata;

	struct v4l2_ctrl *pixel_rate;
};

static inline struct ov5650 *to_ov5650(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5650, subdev);
}

/**
 * struct ov5650_reg - ov5650 register format
 * @reg: 16-bit offset to register
 * @val: 8/16/32-bit register value
 * @length: length of the register
 *
 * Define a structure for OV5650 register initialization values
 */
struct ov5650_reg {
	u16	reg;
	u8	val;
};

static const struct ov5650_reg configscript_common1[] = {
	{ 0x3103, 0x93 },
	{ 0x3b07, 0x0c },
	{ 0x3017, 0xff },
	{ 0x3018, 0xfc },
	{ 0x3706, 0x41 },
	{ 0x3703, 0xe6 },
	{ 0x3613, 0x44 },
	{ 0x3630, 0x22 },
	{ 0x3605, 0x04 },
	{ 0x3606, 0x3f },
	{ 0x3712, 0x13 },
	{ 0x370e, 0x00 },
	{ 0x370b, 0x40 },
	{ 0x3600, 0x54 },
	{ 0x3601, 0x05 },
	{ 0x3713, 0x22 },
	{ 0x3714, 0x27 },
	{ 0x3631, 0x22 },
	{ 0x3612, 0x1a },
	{ 0x3604, 0x40 },
	{ 0x3705, 0xda },
	{ 0x370a, 0x80 },
	{ 0x370c, 0x00 },
	{ 0x3710, 0x28 },
	{ 0x3702, 0x3a },
	{ 0x3704, 0x18 },
	{ 0x3a18, 0x00 },
	{ 0x3a19, 0xf8 },
	{ 0x3a00, 0x38 },
};

static const struct ov5650_reg configscript_common2[] = {
	{ 0x3a08, 0x12 },
	{ 0x3a09, 0x70 },
	{ 0x3a0a, 0x0f },
	{ 0x3a0b, 0x60 },
	{ 0x3a0d, 0x06 },
	{ 0x3a0e, 0x06 },
	{ 0x3a13, 0x54 },
	{ 0x3815, 0x82 },
	{ 0x5059, 0x80 },
	{ 0x505a, 0x0a },
	{ 0x505b, 0x2e },
	{ 0x3a1a, 0x06 },
	{ 0x3503, 0x00 },
	{ 0x3623, 0x01 },
	{ 0x3633, 0x24 },
	{ 0x3c01, 0x34 },
	{ 0x3c04, 0x28 },
	{ 0x3c05, 0x98 },
	{ 0x3c07, 0x07 },
	{ 0x3c09, 0xc2 },
	{ 0x4000, 0x05 },
	{ 0x401d, 0x28 },
	{ 0x4001, 0x02 },
	{ 0x401c, 0x46 },
	{ 0x5046, 0x01 },
	{ 0x3836, 0x41 },
	{ 0x505f, 0x04 },
	{ 0x5000, 0x00 },
	{ 0x5001, 0x00 },
	{ 0x5002, 0x00 },
	{ 0x503d, 0x00 },
	{ 0x5901, 0x00 },
	{ 0x585a, 0x01 },
	{ 0x585b, 0x2c },
	{ 0x585c, 0x01 },
	{ 0x585d, 0x93 },
	{ 0x585e, 0x01 },
	{ 0x585f, 0x90 },
	{ 0x5860, 0x01 },
	{ 0x5861, 0x0d },
	{ 0x5180, 0xc0 },
	{ 0x5184, 0x00 },
	{ 0x470a, 0x00 },
	{ 0x470b, 0x00 },
	{ 0x470c, 0x00 },
	{ 0x3603, 0xa7 },
	{ 0x3615, 0x50 },
	{ 0x3632, 0x55 },
	{ 0x3620, 0x56 },
	{ 0x3621, 0x2f },
	{ 0x381a, 0x3c },
	{ 0x3818, 0xc0 },
	{ 0x3631, 0x36 },
	{ 0x3632, 0x5f },
	{ 0x3711, 0x24 },
	{ 0x401f, 0x03 },
	{ 0x3007, 0x3b },
	{ 0x4801, 0x0f },
	{ 0x3003, 0x03 },
	{ 0x300e, 0x0c },
	{ 0x4803, 0x50 },
	{ 0x4800, 0x04 },
	{ 0x3815, 0x82 },
	{ 0x3003, 0x01 },
};

static const struct ov5650_reg timing_cfg[][16] = {
	[OV5650_SIZE_5MP] = {
		{ 0x3800, 0x02 }, /* HREF Start Point: 596 */
		{ 0x3801, 0x54 },

		{ 0x3803, 0x0c }, /* VREF Start Point: 12 */

		{ 0x3804, 0x0a }, /* HREF Width: 2592 */
		{ 0x3805, 0x20 },

		{ 0x3806, 0x07 }, /* HREF Height: 1944 */
		{ 0x3807, 0x98 },

		{ 0x3808, 0x0a }, /* Horizontal Output Size: 2592 */
		{ 0x3809, 0x20 },

		{ 0x380a, 0x07 }, /* Vertical Output Size: 1944 */
		{ 0x380b, 0x98 },

		{ 0x380c, 0x0c }, /* Total Horizontal Size: 3252 */
		{ 0x380d, 0xb4 },

		{ 0x380e, 0x07 }, /* Total Vertical Size: 1968 */
		{ 0x380f, 0xb0 },

		{ 0x3810, 0x40 },
	},
};

/**
 * ov5650_reg_read - Read a value from a register in an ov5650 sensor device
 * @client: i2c driver client structure
 * @reg: register address / offset
 * @val: stores the value that gets read
 *
 * Read a value from a register in an ov5650 sensor device.
 * The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5650_reg_read(struct i2c_client *client, u16 reg, u8 *val)
{
	int ret;
	u8 data[2] = {0};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 2,
		.buf	= data,
	};

	data[0] = (u8)(reg >> 8);
	data[1] = (u8)(reg & 0xff);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	msg.flags = I2C_M_RD;
	msg.len = 1;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		goto err;

	*val = data[0];
	return 0;

err:
	dev_err(&client->dev, "Failed reading register 0x%02x!\n", reg);
	return ret;
}

/**
 * Write a value to a register in ov5650 sensor device.
 * @client: i2c driver client structure.
 * @reg: Address of the register to read value from.
 * @val: Value to be written to a specific register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5650_reg_write(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	unsigned char data[3] = { (u8)(reg >> 8), (u8)(reg & 0xff), val };
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= data,
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed writing register 0x%02x!\n", reg);
		return ret;
	}

	return 0;
}

/**
 * Initialize a list of ov5650 registers.
 * The list of registers is terminated by the pair of values
 * @client: i2c driver client structure.
 * @reglist[]: List of address of the registers to write data.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ov5650_reg_writes(struct i2c_client *client,
			     const struct ov5650_reg reglist[],
			     int size)
{
	int err = 0, i;

	for (i = 0; i < size; i++) {
		err = ov5650_reg_write(client, reglist[i].reg,
				reglist[i].val);
		if (err)
			return err;
	}
	return 0;
}

static int ov5650_reg_set(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	u8 tmpval = 0;

	ret = ov5650_reg_read(client, reg, &tmpval);
	if (ret)
		return ret;

	return ov5650_reg_write(client, reg, tmpval | val);
}

static int ov5650_reg_clr(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;
	u8 tmpval = 0;

	ret = ov5650_reg_read(client, reg, &tmpval);
	if (ret)
		return ret;

	return ov5650_reg_write(client, reg, tmpval & ~val);
}

static int ov5650_config_timing(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov5650 *ov5650 = to_ov5650(sd);
	int ret = 0, i;

	i = ov5650_find_framesize(ov5650->format.width, ov5650->format.height);

	ret = ov5650_reg_writes(client, timing_cfg[i],
			ARRAY_SIZE(timing_cfg[i]));
	return ret;
};

static struct v4l2_mbus_framefmt *
__ov5650_get_pad_format(struct ov5650 *ov5650, struct v4l2_subdev_fh *fh,
			 unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov5650->format;
	default:
		return NULL;
	}
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev internal operations
 */

static int ov5650_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5650 *ov5650 = to_ov5650(sd);

	return ov5650->pdata->s_power(sd, on);
}

static int ov5650_registered(struct v4l2_subdev *subdev)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct ov5650 *ov5650 = to_ov5650(subdev);
	int ret = 0;
	u8 chipid[2];

	ret = ov5650_s_power(subdev, 1);
	if (ret < 0) {
		dev_err(&client->dev, "OV5650 power up failed\n");
		return ret;
	}

	ret = ov5650_reg_read(client, 0x300A, &chipid[0]);
	if (ret) {
		dev_err(&client->dev, "Failure to read Chip ID (high byte)\n");
		goto out;
	}

	ret = ov5650_reg_read(client, 0x300B, &chipid[1]);
	if (ret) {
		dev_err(&client->dev, "Failure to read Chip ID (low byte)\n");
		goto out;
	}

	if ((chipid[0] != 0x56) || ((chipid[1] & 0x51) != chipid[1])) {
		dev_err(&client->dev, "Chip ID: %x%x not supported!\n",
			chipid[0], chipid[1]);
		ret = -ENODEV;
		goto out;
	}

	dev_info(&client->dev, "Detected a OV%x%x chip, revision %s\n",
			chipid[0], chipid[1],
			(chipid[1] == 0x50) ? "1A": "1B/1C");

	/* Init controls */
	ret = v4l2_ctrl_handler_init(&ov5650->ctrl_handler, 1);
	if (ret)
		goto out;

	ov5650->pixel_rate = v4l2_ctrl_new_std(
		&ov5650->ctrl_handler, NULL,
		V4L2_CID_IMAGE_PROC_PIXEL_RATE,
		0, 0, 1, 0);

	subdev->ctrl_handler = &ov5650->ctrl_handler;
out:
	ov5650_s_power(subdev, 0);

	return ret;
}

static int ov5650_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(fh, 0);
	format->code = V4L2_MBUS_FMT_SGRBG8_1X8;
	format->width = ov5650_frmsizes[OV5650_SIZE_5MP].width;
	format->height = ov5650_frmsizes[OV5650_SIZE_5MP].height;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;

	return 0;
}

static int ov5650_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int ov5650_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov5650 *ov5650 = to_ov5650(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (enable) {
		/* SW Reset */
		ret = ov5650_reg_set(client, 0x3008, 0x80);
		if (ret)
			goto out;

		msleep(2);

		ret = ov5650_reg_clr(client, 0x3008, 0x80);
		if (ret)
			goto out;

		/* SW Powerdown */
		ret = ov5650_reg_set(client, 0x3008, 0x40);
		if (ret)
			goto out;

		ret = ov5650_reg_writes(client, configscript_common1,
				ARRAY_SIZE(configscript_common1));
		if (ret)
			goto out;

		ret = ov5650_config_timing(sd);
		if (ret)
			return ret;

		ret = ov5650_reg_writes(client, configscript_common2,
				ARRAY_SIZE(configscript_common2));
		if (ret)
			goto out;

		switch ((u32)ov5650->format.code) {
		case V4L2_MBUS_FMT_SGRBG8_1X8:
			ret = ov5650_reg_write(client, 0x300f, 0x8a);
			if (ret)
				return ret;

			ret = ov5650_reg_write(client, 0x3010, 0x10);
			if (ret)
				return ret;

			ret = ov5650_reg_write(client, 0x3011, 0x10);
			if (ret)
				return ret;

			ret = ov5650_reg_write(client, 0x3012, 0x02);
			if (ret)
				return ret;

			break;
		case V4L2_MBUS_FMT_SGRBG10_1X10:
			ret = ov5650_reg_write(client, 0x300f, 0x8b);
			if (ret)
				return ret;

			ret = ov5650_reg_write(client, 0x3010, 0x10);
			if (ret)
				return ret;

			ret = ov5650_reg_write(client, 0x3011, 0x14);
			if (ret)
				return ret;

			ret = ov5650_reg_write(client, 0x3012, 0x02);
			if (ret)
				return ret;

			break;
		default:
			/* This shouldn't happen */
			ret = -EINVAL;
			return ret;
		}

		ret = ov5650_reg_clr(client, 0x3008, 0x40);
		if (ret)
			goto out;

	} else {
		ret = ov5650_reg_set(client, 0x3008, 0x40);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static int ov5650_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh,
			struct v4l2_subdev_format *format)
{
	struct ov5650 *ov5650 = to_ov5650(sd);

	format->format = *__ov5650_get_pad_format(ov5650, fh, format->pad,
						   format->which);

	return 0;
}

static int ov5650_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh,
			struct v4l2_subdev_format *format)
{
	struct ov5650 *ov5650 = to_ov5650(sd);
	struct v4l2_mbus_framefmt *__format;

	__format = __ov5650_get_pad_format(ov5650, fh, format->pad,
					    format->which);

	*__format = format->format;

	/* NOTE: This is always true for now, revisit later. */
	/* For RAW10, pixelrate is 80% lower, since the same
	 * Mbps ratio is preserved, but more bits per pixel are
	 * transmitted.
	 */
	if (__format->code == V4L2_MBUS_FMT_SGRBG10_1X10)
		ov5650->pixel_rate->cur.val64 = 96000000;
	else if (__format->code == V4L2_MBUS_FMT_SGRBG8_1X8)
		ov5650->pixel_rate->cur.val64 = 120000000;

	return 0;
}

static struct v4l2_subdev_internal_ops ov5650_subdev_internal_ops = {
	.registered = ov5650_registered,
	.open = ov5650_open,
	.close = ov5650_close,
};

static int ov5650_enum_fmt(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= 2)
		return -EINVAL;

	switch (code->index) {
	case 0:
		code->code = V4L2_MBUS_FMT_SGRBG8_1X8;
		break;
	case 1:
		code->code = V4L2_MBUS_FMT_SGRBG10_1X10;
		break;
	}
	return 0;
}

static int ov5650_enum_framesizes(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if ((fse->index >= OV5650_SIZE_LAST) ||
	    (fse->code != V4L2_MBUS_FMT_SGRBG8_1X8 &&
	     fse->code != V4L2_MBUS_FMT_SGRBG10_1X10))
		return -EINVAL;

	fse->min_width = ov5650_frmsizes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = ov5650_frmsizes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov5650_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	/* REVISIT */
	*frames = 3;

	return 0;
}

static struct v4l2_subdev_core_ops ov5650_subdev_core_ops = {
	.s_power	= ov5650_s_power,
};

static struct v4l2_subdev_video_ops ov5650_subdev_video_ops = {
	.s_stream	= ov5650_s_stream,
};

static struct v4l2_subdev_pad_ops ov5650_subdev_pad_ops = {
	.enum_mbus_code = ov5650_enum_fmt,
	.enum_frame_size = ov5650_enum_framesizes,
	.get_fmt = ov5650_g_fmt,
	.set_fmt = ov5650_s_fmt,
};

static struct v4l2_subdev_sensor_ops ov5650_subdev_sensor_ops = {
	.g_skip_frames	= &ov5650_g_skip_frames,
};

static struct v4l2_subdev_ops ov5650_subdev_ops = {
	.core	= &ov5650_subdev_core_ops,
	.video	= &ov5650_subdev_video_ops,
	.pad	= &ov5650_subdev_pad_ops,
	.sensor	= &ov5650_subdev_sensor_ops,
};

static int ov5650_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
	struct ov5650 *ov5650;
	int ret;

	if (!client->dev.platform_data) {
		dev_err(&client->dev, "No platform data!!\n");
		return -ENODEV;
	}

	ov5650 = kzalloc(sizeof(*ov5650), GFP_KERNEL);
	if (!ov5650)
		return -ENOMEM;

	ov5650->pdata = client->dev.platform_data;

	ov5650->format.code = V4L2_MBUS_FMT_SGRBG8_1X8;
	ov5650->format.width = ov5650_frmsizes[OV5650_SIZE_5MP].width;
	ov5650->format.height = ov5650_frmsizes[OV5650_SIZE_5MP].height;
	ov5650->format.field = V4L2_FIELD_NONE;
	ov5650->format.colorspace = V4L2_COLORSPACE_SRGB;

	v4l2_i2c_subdev_init(&ov5650->subdev, client, &ov5650_subdev_ops);
	ov5650->subdev.internal_ops = &ov5650_subdev_internal_ops;
	ov5650->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov5650->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;

	ov5650->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&ov5650->subdev.entity, 1, &ov5650->pad, 0);

	if (ret < 0) {
		media_entity_cleanup(&ov5650->subdev.entity);
		kfree(ov5650);
	}

	return ret;
}

static int ov5650_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ov5650 *ov5650 = to_ov5650(subdev);

	v4l2_ctrl_handler_free(&ov5650->ctrl_handler);
	v4l2_device_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	kfree(ov5650);
	return 0;
}

static const struct i2c_device_id ov5650_id[] = {
	{ "ov5650", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5650_id);

static struct i2c_driver ov5650_i2c_driver = {
	.driver = {
		.name = "ov5650",
	},
	.probe		= ov5650_probe,
	.remove		= ov5650_remove,
	.id_table	= ov5650_id,
};

static int __init ov5650_mod_init(void)
{
	return i2c_add_driver(&ov5650_i2c_driver);
}

static void __exit ov5650_mod_exit(void)
{
	i2c_del_driver(&ov5650_i2c_driver);
}

module_init(ov5650_mod_init);
module_exit(ov5650_mod_exit);

MODULE_DESCRIPTION("OmniVision OV5650 Camera driver");
MODULE_AUTHOR("Sergio Aguirre <saaguirre@ti.com>");
MODULE_LICENSE("GPL v2");
