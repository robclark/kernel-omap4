#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <plat/i2c.h>
#include <plat/omap-pm.h>

#include <asm/mach-types.h>

#include <media/ov5640.h>
#include <media/ov5650.h>

#include "devices.h"
#include "../../../drivers/media/video/omap4iss/iss.h"

#include "control.h"
#include "mux.h"

#define PANDA_GPIO_CAM_PWRDN		45
#define PANDA_GPIO_CAM_RESET		83

static struct clk *panda_cam_aux_clk;

static int panda_ov5640_power(struct v4l2_subdev *subdev, int on)
{
	struct iss_device *iss = v4l2_dev_to_iss_device(subdev->v4l2_dev);
	int ret = 0;
	struct iss_csiphy_dphy_cfg dphy;
	struct iss_csiphy_lanes_cfg lanes;
	unsigned int ddr_freq = 480; /* FIXME: Do an actual query for this */

	memset(&lanes, 0, sizeof(lanes));
	memset(&dphy, 0, sizeof(dphy));

	lanes.clk.pos = 1;
	lanes.clk.pol = 0;
	lanes.data[0].pos = 2;
	lanes.data[0].pol = 0;
	lanes.data[1].pos = 3;
	lanes.data[1].pol = 0;

	dphy.ths_term = ((((12500 * ddr_freq + 1000000) / 1000000) - 1) & 0xFF);
	dphy.ths_settle = ((((90000 * ddr_freq + 1000000) / 1000000) + 3) & 0xFF);
	dphy.tclk_term = 0;
	dphy.tclk_miss = 1;
	dphy.tclk_settle = 14;

	if (on) {
		gpio_set_value(PANDA_GPIO_CAM_PWRDN, 0);
		clk_enable(panda_cam_aux_clk);
		mdelay(2);

		iss->platform_cb.csiphy_config(&iss->csiphy1, &dphy, &lanes);
	} else {
		clk_disable(panda_cam_aux_clk);
		gpio_set_value(PANDA_GPIO_CAM_PWRDN, 1);
	}

	return ret;
}

#define OV5640_I2C_ADDRESS   (0x3C)
#define OV5650_I2C_ADDRESS   (0x36)

#ifdef CONFIG_MACH_OMAP4_PANDA_CAM_OV5650
static struct ov5650_platform_data ov_platform_data = {
#elif defined(CONFIG_MACH_OMAP4_PANDA_CAM_OV5640)
static struct ov5640_platform_data ov_platform_data = {
#endif
      .s_power = panda_ov5640_power,
};

static struct i2c_board_info ov_camera_i2c_device = {
#ifdef CONFIG_MACH_OMAP4_PANDA_CAM_OV5650
	I2C_BOARD_INFO("ov5650", OV5650_I2C_ADDRESS),
#elif defined(CONFIG_MACH_OMAP4_PANDA_CAM_OV5640)
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

static struct iss_v4l2_subdevs_group panda_camera_subdevs[] = {
	{
		.subdevs = ov_camera_subdevs,
		.interface = ISS_INTERFACE_CSI2A_PHY1,
	},
	{ },
};

static void panda_omap4iss_set_constraints(struct iss_device *iss, bool enable)
{
	if (!iss)
		return;

	/* FIXME: Look for something more precise as a good throughtput limit */
	omap_pm_set_min_bus_tput(iss->dev, OCP_INITIATOR_AGENT,
				 enable ? 800000 : -1);
}

static struct iss_platform_data panda_iss_platform_data = {
	.subdevs = panda_camera_subdevs,
	.set_constraints = panda_omap4iss_set_constraints,
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

static int __init panda_camera_init(void)
{
	if (!machine_is_omap4_panda())
		return 0;

	panda_cam_aux_clk = clk_get(NULL, "auxclk1_ck");
	if (IS_ERR(panda_cam_aux_clk)) {
		printk(KERN_ERR "Unable to get auxclk1_ck\n");
		return -ENODEV;
	}

	if (clk_set_rate(panda_cam_aux_clk,
			clk_round_rate(panda_cam_aux_clk, 24000000)))
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

	/* Select GPIO 45 */
	omap_mux_init_gpio(PANDA_GPIO_CAM_PWRDN, OMAP_PIN_OUTPUT);

	/* Select GPIO 83 */
	omap_mux_init_gpio(PANDA_GPIO_CAM_RESET, OMAP_PIN_OUTPUT);

	/* Init FREF_CLK1_OUT */
	omap_mux_init_signal("fref_clk1_out", OMAP_PIN_OUTPUT);

	if (gpio_request_one(PANDA_GPIO_CAM_PWRDN, GPIOF_OUT_INIT_HIGH,
			     "CAM_PWRDN"))
		printk(KERN_WARNING "Cannot request GPIO %d\n",
			PANDA_GPIO_CAM_PWRDN);

	if (gpio_request_one(PANDA_GPIO_CAM_RESET, GPIOF_OUT_INIT_HIGH,
			     "CAM_RESET"))
		printk(KERN_WARNING "Cannot request GPIO %d\n",
			PANDA_GPIO_CAM_RESET);

	omap4_init_camera(&panda_iss_platform_data, &omap4iss_data);
	return 0;
}
late_initcall(panda_camera_init);
