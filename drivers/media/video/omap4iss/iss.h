/*
 * iss.h
 *
 * Copyright (C) 2011 Texas Instruments.
 *
 * Author: Sergio Aguirre <saaguirre@ti.com>
 */

#ifndef _OMAP4_ISS_H_
#define _OMAP4_ISS_H_

#include <media/v4l2-device.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include <plat/omap4-iss.h>

#include "iss_regs.h"
#include "iss_csiphy.h"
#include "iss_csi2.h"

#define ISS_TOK_TERM		0xFFFFFFFF	/*
						 * terminating token for ISS
						 * modules reg list
						 */
#define to_iss_device(ptr_module)				\
	container_of(ptr_module, struct iss_device, ptr_module)
#define to_device(ptr_module)						\
	(to_iss_device(ptr_module)->dev)

enum iss_mem_resources {
	OMAP4_ISS_MEM_TOP,
	OMAP4_ISS_MEM_CSI2_A_REGS1,
	OMAP4_ISS_MEM_CAMERARX_CORE1,
	OMAP4_ISS_MEM_LAST,
};

enum iss_subclk_resource {
	OMAP4_ISS_SUBCLK_SIMCOP		= (1 << 0),
	OMAP4_ISS_SUBCLK_ISP		= (1 << 1),
	OMAP4_ISS_SUBCLK_CSI2_A		= (1 << 2),
	OMAP4_ISS_SUBCLK_CSI2_B		= (1 << 3),
	OMAP4_ISS_SUBCLK_CCP2		= (1 << 4),
};

/*
 * struct iss_reg - Structure for ISS register values.
 * @reg: 32-bit Register address.
 * @val: 32-bit Register value.
 */
struct iss_reg {
	enum iss_mem_resources mmio_range;
	u32 reg;
	u32 val;
};

struct iss_platform_callback {
	int (*csiphy_config)(struct iss_csiphy *phy,
			     struct iss_csiphy_dphy_cfg *dphy,
			     struct iss_csiphy_lanes_cfg *lanes);
};

struct iss_device {
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct device *dev;
	u32 revision;

	/* platform HW resources */
	struct iss_platform_data *pdata;
	unsigned int irq_num;

	struct resource *res[OMAP4_ISS_MEM_LAST];
	void __iomem *regs[OMAP4_ISS_MEM_LAST];

	u64 raw_dmamask;

	struct mutex iss_mutex;	/* For handling ref_count field */
	int has_context;
	int ref_count;

	struct clk *iss_fck;
	struct clk *iss_ctrlclk;

	/* ISS modules */
	struct iss_csi2_device csi2a;
	struct iss_csiphy csiphy1;

	unsigned int subclk_resources;

	struct iss_platform_callback platform_cb;
};

#define v4l2_dev_to_iss_device(dev) \
	container_of(dev, struct iss_device, v4l2_dev)

int omap4iss_module_sync_idle(struct media_entity *me, wait_queue_head_t *wait,
			      atomic_t *stopping);

int omap4iss_module_sync_is_stopping(wait_queue_head_t *wait,
				     atomic_t *stopping);

int omap4iss_pipeline_set_stream(struct iss_pipeline *pipe,
				 enum iss_pipeline_stream_state state);

struct iss_device *omap4iss_get(struct iss_device *iss);
void omap4iss_put(struct iss_device *iss);
int omap4iss_subclk_enable(struct iss_device *iss,
			   enum iss_subclk_resource res);
int omap4iss_subclk_disable(struct iss_device *iss,
			    enum iss_subclk_resource res);

int omap4iss_pipeline_pm_use(struct media_entity *entity, int use);

int omap4iss_register_entities(struct platform_device *pdev,
			       struct v4l2_device *v4l2_dev);
void omap4iss_unregister_entities(struct platform_device *pdev);

static inline enum v4l2_buf_type
iss_pad_buffer_type(const struct v4l2_subdev *subdev, int pad)
{
	if (pad >= subdev->entity.num_pads)
		return 0;

	if (subdev->entity.pads[pad].flags & MEDIA_PAD_FL_SINK)
		return V4L2_BUF_TYPE_VIDEO_OUTPUT;
	else
		return V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

#endif /* _OMAP4_ISS_H_ */
