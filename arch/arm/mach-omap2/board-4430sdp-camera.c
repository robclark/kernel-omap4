#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/twl6040.h>

#include <plat/i2c.h>
#include <plat/omap-pm.h>

#include <asm/mach-types.h>

#include <media/ov5640.h>
#include <media/ov5650.h>

#include "devices.h"
#include "../../../drivers/media/video/omap4iss/iss.h"

#include "control.h"
#include "mux.h"

#define OMAP4430SDP_GPIO_CAM_PDN_C	39

static struct clk *sdp4430_cam_aux_clk;

static int sdp4430_ov5640_power(struct v4l2_subdev *subdev, int on)
{
	struct iss_device *iss = v4l2_dev_to_iss_device(subdev->v4l2_dev);
	int ret = 0;
	struct iss_csiphy_dphy_cfg dphy;
	struct iss_csiphy_lanes_cfg lanes;
#ifdef CONFIG_MACH_OMAP_4430SDP_CAM_OV5650
	unsigned int ddr_freq = 480; /* FIXME: Do an actual query for this */
#elif defined(CONFIG_MACH_OMAP_4430SDP_CAM_OV5640)
	unsigned int ddr_freq = 336; /* FIXME: Do an actual query for this */
#endif

	memset(&lanes, 0, sizeof(lanes));
	memset(&dphy, 0, sizeof(dphy));

	lanes.clk.pos = 1;
	lanes.clk.pol = 0;
	lanes.data[0].pos = 2;
	lanes.data[0].pol = 0;
#ifdef CONFIG_MACH_OMAP_4430SDP_CAM_OV5650
	lanes.data[1].pos = 3;
	lanes.data[1].pol = 0;
#endif

	dphy.ths_term = ((((12500 * ddr_freq + 1000000) / 1000000) - 1) & 0xFF);
	dphy.ths_settle = ((((90000 * ddr_freq + 1000000) / 1000000) + 3) & 0xFF);
	dphy.tclk_term = 0;
	dphy.tclk_miss = 1;
	dphy.tclk_settle = 14;

	if (on) {
		u8 gpoctl = 0;

		/* TWL6030_BASEADD_AUX */
		twl_i2c_write_u8(15, 0x00, 0xB);
		twl_i2c_write_u8(15, 0x80, 0x1);

		mdelay(50);

		/* TWL6030_BASEADD_PM_SLAVE_MISC */
		twl_i2c_write_u8(21, 0xFF, 0x5E);
		twl_i2c_write_u8(21, 0x13, 0x5F);

		mdelay(50);

		twl_i2c_write_u8(15, 0x40, 0x1);

		twl_i2c_read_u8(TWL_MODULE_AUDIO_VOICE, &gpoctl,
				TWL6040_REG_GPOCTL);
		twl_i2c_write_u8(TWL_MODULE_AUDIO_VOICE, gpoctl | TWL6040_GPO3,
				TWL6040_REG_GPOCTL);

		mdelay(10);

		gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_C, 1);
		mdelay(10);
		clk_enable(sdp4430_cam_aux_clk); /* Enable XCLK */
		mdelay(10);

		iss->platform_cb.csiphy_config(&iss->csiphy1, &dphy, &lanes);
	} else {
		clk_disable(sdp4430_cam_aux_clk);
		mdelay(1);
		gpio_set_value(OMAP4430SDP_GPIO_CAM_PDN_C, 0);
	}

	return ret;
}

#define OV5640_I2C_ADDRESS   (0x3C)
#define OV5650_I2C_ADDRESS   (0x36)

#ifdef CONFIG_MACH_OMAP_4430SDP_CAM_OV5650
static struct ov5650_platform_data ov_platform_data = {
#elif defined(CONFIG_MACH_OMAP_4430SDP_CAM_OV5640)
static struct ov5640_platform_data ov_platform_data = {
#endif
      .s_power = sdp4430_ov5640_power,
};

static struct i2c_board_info ov_camera_i2c_device = {
#ifdef CONFIG_MACH_OMAP_4430SDP_CAM_OV5650
	I2C_BOARD_INFO("ov5650", OV5650_I2C_ADDRESS),
#elif defined(CONFIG_MACH_OMAP_4430SDP_CAM_OV5640)
	I2C_BOARD_INFO("ov5640", OV5640_I2C_ADDRESS),
#endif
	.platform_data = &ov_platform_data,
};

static struct iss_subdev_i2c_board_info ov_camera_subdevs[] = {
	{
		.board_info = &ov_camera_i2c_device,
		.i2c_adapter_id = 3,
	},
	{ NULL, 0, },
};

static struct iss_v4l2_subdevs_group sdp4430_camera_subdevs[] = {
	{
		.subdevs = ov_camera_subdevs,
		.interface = ISS_INTERFACE_CSI2A_PHY1,
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

	sdp4430_cam_aux_clk = clk_get(NULL, "auxclk3_ck");
	if (IS_ERR(sdp4430_cam_aux_clk)) {
		printk(KERN_ERR "Unable to get auxclk3_ck\n");
		return -ENODEV;
	}

	if (clk_set_rate(sdp4430_cam_aux_clk,
			clk_round_rate(sdp4430_cam_aux_clk, 24000000)))
		return -EINVAL;

	/*
	 * CSI2 1(A):
	 *   LANEENABLE[4:0] = 00111(0x7) - Lanes 0, 1 & 2 enabled
	 *   CTRLCLKEN = 1 - Active high enable for CTRLCLK
	 *   CAMMODE = 0 - DPHY mode
	 */
	omap4_ctrl_pad_writel((omap4_ctrl_pad_readl(
				OMAP4_CTRL_MODULE_PAD_CORE_CONTROL_CAMERA_RX) &
			  ~(OMAP4_CAMERARX_CSI21_LANEENABLE_MASK |
			    OMAP4_CAMERARX_CSI21_CAMMODE_MASK)) |
			 (0x7 << OMAP4_CAMERARX_CSI21_LANEENABLE_SHIFT) |
			 OMAP4_CAMERARX_CSI21_CTRLCLKEN_MASK,
			 OMAP4_CTRL_MODULE_PAD_CORE_CONTROL_CAMERA_RX);

	omap_mux_init_gpio(OMAP4430SDP_GPIO_CAM_PDN_C, OMAP_PIN_OUTPUT);

	/* Init FREF_CLK3_OUT */
	omap_mux_init_signal("fref_clk3_out", OMAP_PIN_OUTPUT);

	if (gpio_request(OMAP4430SDP_GPIO_CAM_PDN_C, "CAM_PDN_C"))
		printk(KERN_WARNING "Cannot request GPIO %d\n",
			OMAP4430SDP_GPIO_CAM_PDN_C);
	else
		gpio_direction_output(OMAP4430SDP_GPIO_CAM_PDN_C, 0);

	omap4_init_camera(&sdp4430_iss_platform_data, &omap4iss_data);
	return 0;
}
late_initcall(sdp4430_camera_init);
