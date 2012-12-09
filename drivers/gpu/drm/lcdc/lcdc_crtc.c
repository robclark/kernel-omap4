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

#include "lcdc_drv.h"
#include "lcdc_regs.h"

struct lcdc_crtc {
	struct drm_crtc base;

	struct lcdc_panel_info *info;
	uint32_t dirty;
	dma_addr_t start, end;
	struct drm_pending_vblank_event *event;
	int dpms;
};
#define to_lcdc_crtc(x) container_of(x, struct lcdc_crtc, base)

static void set_scanout(struct drm_crtc *crtc, uint32_t stat)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;

	pm_runtime_get_sync(dev->dev);
	if (stat & LCDC_END_OF_FRAME0) {
		lcdc_write(dev, LCDC_DMA_FRM_BUF_BASE_ADDR_0_REG,
				lcdc_crtc->start);
		lcdc_write(dev, LCDC_DMA_FRM_BUF_CEILING_ADDR_0_REG,
				lcdc_crtc->end);
	} else if (stat & LCDC_END_OF_FRAME1) {
		lcdc_write(dev, LCDC_DMA_FRM_BUF_BASE_ADDR_1_REG,
				lcdc_crtc->start);
		lcdc_write(dev, LCDC_DMA_FRM_BUF_CEILING_ADDR_1_REG,
				lcdc_crtc->end);
	}
	pm_runtime_put_sync(dev->dev);
}

static void update_scanout(struct drm_crtc *crtc)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_framebuffer *fb = crtc->fb;
	struct drm_gem_cma_object *gem;
	unsigned int depth, bpp;

	drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	lcdc_crtc->start = gem->paddr + fb->offsets[0] +
			(crtc->y * fb->pitches[0]) + (crtc->x * bpp/8);

	lcdc_crtc->end = lcdc_crtc->start +
			(crtc->mode.vdisplay * fb->pitches[0]);


	if (lcdc_crtc->dpms == DRM_MODE_DPMS_ON) {
		/* already enabled, so just mark the frames that need
		 * updating and they will be updated on vblank:
		 */
		lcdc_crtc->dirty |= LCDC_END_OF_FRAME0 | LCDC_END_OF_FRAME1;
		drm_vblank_get(dev, 0);
	} else {
		/* not enabled yet, so just update registers directly: */
		set_scanout(crtc, LCDC_END_OF_FRAME0 | LCDC_END_OF_FRAME1);
	}
}

static void start(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct lcdc_drm_private *priv = dev->dev_private;

	if (priv->rev == 2) {
		lcdc_set(dev, LCDC_CLK_RESET_REG, LCDC_CLK_MAIN_RESET);
		msleep(1);
		lcdc_clear(dev, LCDC_CLK_RESET_REG, LCDC_CLK_MAIN_RESET);
		msleep(1);
	}

	lcdc_set(dev, LCDC_DMA_CTRL_REG, LCDC_DUAL_FRAME_BUFFER_ENABLE);
	lcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_PALETTE_LOAD_MODE(DATA_ONLY));
	lcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ENABLE);
}

static void stop(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;

	lcdc_clear(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ENABLE);
}

static void lcdc_crtc_destroy(struct drm_crtc *crtc)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);

	WARN_ON(lcdc_crtc->dpms == DRM_MODE_DPMS_ON);

	drm_crtc_cleanup(crtc);
	kfree(lcdc_crtc);
}

static int lcdc_crtc_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;

	if (lcdc_crtc->event) {
		dev_err(dev->dev, "already pending page flip!\n");
		return -EBUSY;
	}

	// TODO we should hold a ref to the fb somewhere..
	crtc->fb = fb;
	lcdc_crtc->event = event;
	update_scanout(crtc);

	return 0;
}

static void lcdc_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;

	if (lcdc_crtc->dpms == mode)
		return;

	if (mode == DRM_MODE_DPMS_ON) {
		pm_runtime_get_sync(dev->dev);
		start(crtc);
	} else {
		stop(crtc);
		pm_runtime_put_sync(dev->dev);
	}

	lcdc_crtc->dpms = mode;
}

static bool lcdc_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void lcdc_crtc_prepare(struct drm_crtc *crtc)
{
	lcdc_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void lcdc_crtc_commit(struct drm_crtc *crtc)
{
	lcdc_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int lcdc_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	update_scanout(crtc);
	lcdc_crtc_update_clk(crtc);
	// TODO set timings
	return 0;
}

static int lcdc_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	update_scanout(crtc);
	return 0;
}

static void lcdc_crtc_load_lut(struct drm_crtc *crtc)
{
	/* TODO */
}

static const struct drm_crtc_funcs lcdc_crtc_funcs = {
		.destroy        = lcdc_crtc_destroy,
		.set_config     = drm_crtc_helper_set_config,
		.page_flip      = lcdc_crtc_page_flip,
};

static const struct drm_crtc_helper_funcs lcdc_crtc_helper_funcs = {
		.dpms           = lcdc_crtc_dpms,
		.mode_fixup     = lcdc_crtc_mode_fixup,
		.prepare        = lcdc_crtc_prepare,
		.commit         = lcdc_crtc_commit,
		.mode_set       = lcdc_crtc_mode_set,
		.mode_set_base  = lcdc_crtc_mode_set_base,
		.load_lut       = lcdc_crtc_load_lut,
};

void lcdc_crtc_set_panel_info(struct drm_crtc *crtc,
		struct lcdc_panel_info *info)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	lcdc_crtc->info = info;
}

void lcdc_crtc_update_clk(struct drm_crtc *crtc)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct lcdc_drm_private *priv = dev->dev_private;
	int dpms = lcdc_crtc->dpms;
	unsigned int lcd_clk, div;

	pm_runtime_get_sync(dev->dev);

	if (dpms == DRM_MODE_DPMS_ON)
		lcdc_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	lcd_clk = clk_get_rate(priv->clk);
	div = lcd_clk / lcdc_crtc->info->pxl_clk;

	/* Configure the LCD clock divisor. */
	lcdc_write(dev, LCDC_CTRL_REG, LCDC_CLK_DIVISOR(div) |
			LCDC_RASTER_MODE);

	if (priv->rev == 2)
		lcdc_set(dev, LCDC_CLK_ENABLE_REG,
				LCDC_V2_DMA_CLK_EN | LCDC_V2_LIDD_CLK_EN |
				LCDC_V2_CORE_CLK_EN);

	if (dpms == DRM_MODE_DPMS_ON)
		lcdc_crtc_dpms(crtc, DRM_MODE_DPMS_ON);

	pm_runtime_put_sync(dev->dev);
}

irqreturn_t lcdc_crtc_irq(struct drm_crtc *crtc)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct lcdc_drm_private *priv = dev->dev_private;
	uint32_t stat = lcdc_read_irqstatus(dev);

	if ((stat & LCDC_SYNC_LOST) && (stat & LCDC_FIFO_UNDERFLOW)) {
		stop(crtc);
		lcdc_clear_irqstatus(dev, stat);
		start(crtc);
	} else if (stat & LCDC_PL_LOAD_DONE) {
		/* TODO lut load.. */
		lcdc_clear_irqstatus(dev, stat);
	} else {
		struct drm_pending_vblank_event *event;
		unsigned long flags;

		lcdc_clear_irqstatus(dev, stat);

		if (stat & lcdc_crtc->dirty)
			set_scanout(crtc, stat);

		drm_handle_vblank(dev, 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		event = lcdc_crtc->event;
		lcdc_crtc->event = NULL;
		if (event) {
			drm_send_vblank_event(dev, 0, event);
			drm_vblank_put(dev, 0);
		}
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	if (priv->rev == 2)
		lcdc_write(dev, LCDC_END_OF_INT_IND_REG, 0);

	return IRQ_HANDLED;
}

void lcdc_crtc_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	/* Destroy the pending vertical blanking event associated with the
	 * pending page flip, if any, and disable vertical blanking interrupts.
	 */
	spin_lock_irqsave(&dev->event_lock, flags);
	event = lcdc_crtc->event;
	if (event && event->base.file_priv == file) {
		lcdc_crtc->event = NULL;
		event->base.destroy(&event->base);
		drm_vblank_put(dev, 0);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

struct drm_crtc *lcdc_crtc_create(struct drm_device *dev)
{
	struct lcdc_crtc *lcdc_crtc;
	struct drm_crtc *crtc;
	int ret;

	lcdc_crtc = kzalloc(sizeof(*lcdc_crtc), GFP_KERNEL);
	if (!lcdc_crtc) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	lcdc_crtc->dpms = DRM_MODE_DPMS_OFF;
	crtc = &lcdc_crtc->base;

	ret = drm_crtc_init(dev, crtc, &lcdc_crtc_funcs);
	if (ret < 0)
		goto fail;

	drm_crtc_helper_add(crtc, &lcdc_crtc_helper_funcs);

	return crtc;

fail:
	lcdc_crtc_destroy(crtc);
	return NULL;
}
