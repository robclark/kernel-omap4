/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LCDC_DRV_H__
#define __LCDC_DRV_H__

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

struct lcdc_drm_private {
	void __iomem *mmio;
	struct clk *clk;

	/* IP revision: */
	int rev;

	/* register contents saved across suspend/resume: */
	u32 saved_register[12];

	spinlock_t irq_lock; // TODO do we need this?

	struct drm_fbdev_cma *fbdev;

	/* just simple hw, with single crtc/encoder/connector: */
	struct drm_crtc      *crtc;
	struct drm_encoder   *encoder;
	struct drm_connector *connector;
};


struct drm_crtc *lcdc_crtc_create(struct drm_device *dev);
void lcdc_crtc_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file);

struct drm_encoder *lcdc_encoder_create(struct drm_device *dev);

struct drm_connector *lcdc_connector_create(struct drm_device *dev,
		struct drm_encoder *encoder);

#endif /* __LCDC_DRV_H__ */
