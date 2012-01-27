#ifndef ARCH_ARM_PLAT_OMAP4_ISS_H
#define ARCH_ARM_PLAT_OMAP4_ISS_H

#include <linux/i2c.h>

struct iss_device;

enum iss_interface_type {
	ISS_INTERFACE_CSI2A_PHY1,
};

/**
 * struct iss_csi2_platform_data - CSI2 interface platform data
 * @crc: Enable the cyclic redundancy check
 * @vpclk_div: Video port output clock control
 */
struct iss_csi2_platform_data {
	unsigned crc:1;
	unsigned vpclk_div:2;
};

struct iss_subdev_i2c_board_info {
	struct i2c_board_info *board_info;
	int i2c_adapter_id;
};

struct iss_v4l2_subdevs_group {
	struct iss_subdev_i2c_board_info *subdevs;
	enum iss_interface_type interface;
	union {
		struct iss_csi2_platform_data csi2;
	} bus; /* gcc < 4.6.0 chokes on anonymous union initializers */
};

struct iss_platform_data {
	struct iss_v4l2_subdevs_group *subdevs;
	void (*set_constraints)(struct iss_device *iss, bool enable);
};

extern int omap4_init_camera(struct iss_platform_data *pdata,
			     struct omap_board_data *bdata);
#endif
