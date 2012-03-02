#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/twl6040.h>
#include <linux/regulator/consumer.h>

#include <plat/i2c.h>
#include <plat/omap-pm.h>

#include <asm/mach-types.h>

#include <media/ov5640.h>
#include <media/ov5650.h>

#include "devices.h"
#include "../../../drivers/media/video/omap4iss/iss.h"

#include "control.h"
#include "mux.h"

#define OMAP4430SDP_GPIO_CAM_PDN_B	38
#define OMAP4430SDP_GPIO_CAM_PDN_C	39

static struct clk *sdp4430_cam1_aux_clk;
static struct clk *sdp4430_cam2_aux_clk;
static struct regulator *sdp4430_cam2pwr_reg;

static int sdp4430_ov_cam1_power(struct v4l2_subdev *subdev, int on)
{
	struct device *dev = subdev->v4l2_dev->dev;
	int ret;

	if (on) {
		if (!regulator_is_enabled(sdp4430_cam2pwr_reg)) {
			ret = regulator_enable(sdp4430_cam2pwr_reg);
			if (ret) {
				dev_err(dev,
					"Error in enabling sensor power regulator 'cam2pwr'\n");
				return ret;
			}

			msleep(50);
		}

		gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_B, 1);
		msleep(10);
		ret = clk_enable(sdp4430_cam1_aux_clk); /* Enable XCLK */
		if (ret) {
			dev_err(dev,
				"Error in clk_enable() in %s(%d)\n",
				__func__, on);
			gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_B, 0);
			return ret;
		}
		msleep(10);
	} else {
		clk_disable(sdp4430_cam1_aux_clk);
		msleep(1);
		gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_B, 0);
		if (regulator_is_enabled(sdp4430_cam2pwr_reg)) {
			ret = regulator_disable(sdp4430_cam2pwr_reg);
			if (ret) {
				dev_err(dev,
					"Error in disabling sensor power regulator 'cam2pwr'\n");
				return ret;
			}
		}
	}

	return 0;
}

static int sdp4430_ov_cam2_power(struct v4l2_subdev *subdev, int on)
{
	struct device *dev = subdev->v4l2_dev->dev;
	int ret;

	if (on) {
		u8 gpoctl = 0;

		ret = regulator_enable(sdp4430_cam2pwr_reg);
		if (ret) {
			dev_err(dev,
				"Error in enabling sensor power regulator 'cam2pwr'\n");
			return ret;
		}

		msleep(50);

		if (twl_i2c_read_u8(TWL_MODULE_AUDIO_VOICE, &gpoctl,
				    TWL6040_REG_GPOCTL))
			return -ENODEV;
		if (twl_i2c_write_u8(TWL_MODULE_AUDIO_VOICE,
				     gpoctl | TWL6040_GPO3,
				     TWL6040_REG_GPOCTL))
			return -ENODEV;

		msleep(10);

		gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_C, 1);
		msleep(10);
		ret = clk_enable(sdp4430_cam2_aux_clk); /* Enable XCLK */
		if (ret) {
			dev_err(dev,
				"Error in clk_enable() in %s(%d)\n",
				__func__, on);
			gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_C, 0);
			return ret;
		}
		msleep(10);
	} else {
		clk_disable(sdp4430_cam2_aux_clk);
		msleep(1);
		gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_C, 0);
		if (regulator_is_enabled(sdp4430_cam2pwr_reg)) {
			ret = regulator_disable(sdp4430_cam2pwr_reg);
			if (ret) {
				dev_err(dev,
					"Error in disabling sensor power regulator 'cam2pwr'\n");
				return ret;
			}
		}
	}

	return 0;
}

#define OV5640_I2C_ADDRESS   (0x3C)

static struct ov5640_platform_data ov5640_cam1_platform_data = {
	.s_power = sdp4430_ov_cam1_power,
};

static struct i2c_board_info ov5640_cam1_i2c_device = {
	I2C_BOARD_INFO("ov5640", OV5640_I2C_ADDRESS),
	.platform_data = &ov5640_cam1_platform_data,
};

static struct ov5640_platform_data ov5640_cam2_platform_data = {
	.s_power = sdp4430_ov_cam2_power,
};

static struct i2c_board_info ov5640_cam2_i2c_device = {
	I2C_BOARD_INFO("ov5640", OV5640_I2C_ADDRESS),
	.platform_data = &ov5640_cam2_platform_data,
};

#define OV5650_I2C_ADDRESS   (0x36)

static struct ov5650_platform_data ov5650_cam1_platform_data = {
	.s_power = sdp4430_ov_cam1_power,
};

static struct i2c_board_info ov5650_cam1_i2c_device = {
	I2C_BOARD_INFO("ov5650", OV5650_I2C_ADDRESS),
	.platform_data = &ov5650_cam1_platform_data,
};

static struct ov5650_platform_data ov5650_cam2_platform_data = {
	.s_power = sdp4430_ov_cam2_power,
};

static struct i2c_board_info ov5650_cam2_i2c_device = {
	I2C_BOARD_INFO("ov5650", OV5650_I2C_ADDRESS),
	.platform_data = &ov5650_cam2_platform_data,
};

static struct iss_subdev_i2c_board_info ov5640_cam1_subdevs[] = {
	{
		.board_info = &ov5640_cam1_i2c_device,
		.i2c_adapter_id = 2,
	},
	{ NULL, 0, },
};

static struct iss_subdev_i2c_board_info ov5650_cam1_subdevs[] = {
	{
		.board_info = &ov5650_cam1_i2c_device,
		.i2c_adapter_id = 2,
	},
	{ NULL, 0, },
};

static struct iss_subdev_i2c_board_info ov5640_cam2_subdevs[] = {
	{
		.board_info = &ov5640_cam2_i2c_device,
		.i2c_adapter_id = 3,
	},
	{ NULL, 0, },
};

static struct iss_subdev_i2c_board_info ov5650_cam2_subdevs[] = {
	{
		.board_info = &ov5650_cam2_i2c_device,
		.i2c_adapter_id = 3,
	},
	{ NULL, 0, },
};

static struct iss_v4l2_subdevs_group sdp4430_camera_subdevs[] = {
	{
		.subdevs = ov5640_cam1_subdevs,
		.interface = ISS_INTERFACE_CSI2B_PHY2,
		.bus = { .csi2 = {
			.lanecfg	= {
				.clk = {
					.pol = 0,
					.pos = 1,
				},
				.data[0] = {
					.pol = 0,
					.pos = 2,
				},
			},
		} },
	},
	{
		.subdevs = ov5650_cam1_subdevs,
		.interface = ISS_INTERFACE_CSI2B_PHY2,
		.bus = { .csi2 = {
			.lanecfg	= {
				.clk = {
					.pol = 0,
					.pos = 1,
				},
				.data[0] = {
					.pol = 0,
					.pos = 2,
				},
			},
		} },
	},
	{
		.subdevs = ov5640_cam2_subdevs,
		.interface = ISS_INTERFACE_CSI2A_PHY1,
		.bus = { .csi2 = {
			.lanecfg	= {
				.clk = {
					.pol = 0,
					.pos = 1,
				},
				.data[0] = {
					.pol = 0,
					.pos = 2,
				},
			},
		} },
	},
	{
		.subdevs = ov5650_cam2_subdevs,
		.interface = ISS_INTERFACE_CSI2A_PHY1,
		.bus = { .csi2 = {
			.lanecfg	= {
				.clk = {
					.pol = 0,
					.pos = 1,
				},
				.data[0] = {
					.pol = 0,
					.pos = 2,
				},
			},
		} },
	},
	{ },
};

static void sdp4430_omap4iss_set_constraints(struct iss_device *iss, bool enable)
{
	if (!iss)
		return;

	/* FIXME: Look for something more precise as a good throughtput limit */
	omap_pm_set_min_bus_tput(iss->dev, OCP_INITIATOR_AGENT,
				 enable ? 800000 : -1);
}

static struct iss_platform_data sdp4430_iss_platform_data = {
	.subdevs = sdp4430_camera_subdevs,
	.set_constraints = sdp4430_omap4iss_set_constraints,
};

static struct omap_device_pad omap4iss_pads[] = {
	/* CSI2-A */
	{
		.name   = "csi21_dx0.csi21_dx0",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi21_dy0.csi21_dy0",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi21_dx1.csi21_dx1",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi21_dy1.csi21_dy1",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi21_dx2.csi21_dx2",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi21_dy2.csi21_dy2",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	/* CSI2-B */
	{
		.name   = "csi22_dx0.csi22_dx0",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi22_dy0.csi22_dy0",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi22_dx1.csi22_dx1",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
	{
		.name   = "csi22_dy1.csi22_dy1",
		.enable = OMAP_MUX_MODE0 | OMAP_INPUT_EN,
	},
};

static struct omap_board_data omap4iss_data = {
	.id	    		= 1,
	.pads	 		= omap4iss_pads,
	.pads_cnt       	= ARRAY_SIZE(omap4iss_pads),
};

static int __init sdp4430_camera_init(void)
{
	if (!machine_is_omap_4430sdp())
		return 0;

	sdp4430_cam2pwr_reg = regulator_get(NULL, "cam2pwr");
	if (IS_ERR(sdp4430_cam2pwr_reg)) {
		printk(KERN_ERR "Unable to get 'cam2pwr' regulator for sensor power\n");
		return -ENODEV;
	}
	
	if (regulator_set_voltage(sdp4430_cam2pwr_reg, 2600000, 3100000)) {
		printk(KERN_ERR "Unable to set valid 'cam2pwr' regulator"
			" voltage range to: 2.6V ~ 3.1V\n");
		regulator_put(sdp4430_cam2pwr_reg);
		return -ENODEV;
	}

	sdp4430_cam1_aux_clk = clk_get(NULL, "auxclk2_ck");
	if (IS_ERR(sdp4430_cam1_aux_clk)) {
		printk(KERN_ERR "Unable to get auxclk2_ck\n");
		regulator_put(sdp4430_cam2pwr_reg);
		return -ENODEV;
	}

	if (clk_set_rate(sdp4430_cam1_aux_clk,
			clk_round_rate(sdp4430_cam1_aux_clk, 24000000))) {
		clk_put(sdp4430_cam1_aux_clk);
		regulator_put(sdp4430_cam2pwr_reg);
		return -EINVAL;
	}

	sdp4430_cam2_aux_clk = clk_get(NULL, "auxclk3_ck");
	if (IS_ERR(sdp4430_cam2_aux_clk)) {
		printk(KERN_ERR "Unable to get auxclk3_ck\n");
		clk_put(sdp4430_cam1_aux_clk);
		regulator_put(sdp4430_cam2pwr_reg);
		return -ENODEV;
	}

	if (clk_set_rate(sdp4430_cam2_aux_clk,
			clk_round_rate(sdp4430_cam2_aux_clk, 24000000))) {
		clk_put(sdp4430_cam1_aux_clk);
		clk_put(sdp4430_cam2_aux_clk);
		regulator_put(sdp4430_cam2pwr_reg);
		return -EINVAL;
	}

	omap_mux_init_gpio(OMAP4430SDP_GPIO_CAM_PDN_B, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(OMAP4430SDP_GPIO_CAM_PDN_C, OMAP_PIN_OUTPUT);

	/* Init FREF_CLK2_OUT */
	omap_mux_init_signal("fref_clk2_out", OMAP_PIN_OUTPUT);

	/* Init FREF_CLK3_OUT */
	omap_mux_init_signal("fref_clk3_out", OMAP_PIN_OUTPUT);

	if (gpio_request(OMAP4430SDP_GPIO_CAM_PDN_B, "CAM_PDN_B"))
		printk(KERN_WARNING "Cannot request GPIO %d\n",
			OMAP4430SDP_GPIO_CAM_PDN_B);
	else
		gpio_direction_output(OMAP4430SDP_GPIO_CAM_PDN_B, 0);

	if (gpio_request(OMAP4430SDP_GPIO_CAM_PDN_C, "CAM_PDN_C"))
		printk(KERN_WARNING "Cannot request GPIO %d\n",
			OMAP4430SDP_GPIO_CAM_PDN_C);
	else
		gpio_direction_output(OMAP4430SDP_GPIO_CAM_PDN_C, 0);

	if (omap4_init_camera(&sdp4430_iss_platform_data, &omap4iss_data)) {
		gpio_free(OMAP4430SDP_GPIO_CAM_PDN_B);
		gpio_free(OMAP4430SDP_GPIO_CAM_PDN_C);
		regulator_put(sdp4430_cam2pwr_reg);
		clk_put(sdp4430_cam1_aux_clk);
		clk_put(sdp4430_cam2_aux_clk);
		return -ENODEV;
	}

	return 0;
}
late_initcall(sdp4430_camera_init);
