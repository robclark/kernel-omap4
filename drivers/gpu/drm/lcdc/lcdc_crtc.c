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

#include <linux/kfifo.h>

#include "lcdc_drv.h"
#include "lcdc_regs.h"

struct lcdc_crtc {
	struct drm_crtc base;

	const struct lcdc_panel_info *info;
	uint32_t dirty;
	dma_addr_t start, end;
	struct drm_pending_vblank_event *event;
	int dpms;
	wait_queue_head_t frame_done_wq;
	bool frame_done;

	/* fb currently set to scanout 0/1: */
	struct drm_framebuffer *scanout[2];

	/* for deferred fb unref's: */
	DECLARE_KFIFO_PTR(unref_fifo, struct drm_framebuffer *);
	struct work_struct work;
};
#define to_lcdc_crtc(x) container_of(x, struct lcdc_crtc, base)

static void unref_worker(struct work_struct *work)
{
	struct lcdc_crtc *lcdc_crtc = container_of(work, struct lcdc_crtc, work);
	struct drm_device *dev = lcdc_crtc->base.dev;
	struct drm_framebuffer *fb;

	mutex_lock(&dev->mode_config.mutex);
	while (kfifo_get(&lcdc_crtc->unref_fifo, &fb))
		drm_framebuffer_unreference(fb);
	mutex_unlock(&dev->mode_config.mutex);
}

static void set_scanout(struct drm_crtc *crtc, int n)
{
	static const uint32_t base_reg[] = {
			LCDC_DMA_FB_BASE_ADDR_0_REG, LCDC_DMA_FB_BASE_ADDR_1_REG,
	};
	static const uint32_t ceil_reg[] = {
			LCDC_DMA_FB_CEILING_ADDR_0_REG, LCDC_DMA_FB_CEILING_ADDR_1_REG,
	};
	static const uint32_t stat[] = {
			LCDC_END_OF_FRAME0, LCDC_END_OF_FRAME1,
	};
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;

	pm_runtime_get_sync(dev->dev);
	lcdc_write(dev, base_reg[n], lcdc_crtc->start);
	lcdc_write(dev, ceil_reg[n], lcdc_crtc->end);
	if (lcdc_crtc->scanout[n]) {
		if (kfifo_put(&lcdc_crtc->unref_fifo,
				(const struct drm_framebuffer **)&lcdc_crtc->scanout[n])) {
			struct lcdc_drm_private *priv = dev->dev_private;
			queue_work(priv->wq, &lcdc_crtc->work);
		} else {
			dev_err(dev->dev, "unref fifo full!\n");
			drm_framebuffer_unreference(lcdc_crtc->scanout[n]);
		}
	}
	lcdc_crtc->scanout[n] = crtc->fb;
	drm_framebuffer_reference(lcdc_crtc->scanout[n]);
	lcdc_crtc->dirty &= ~stat[n];
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
		/* not enabled yet, so update registers immediately: */
		set_scanout(crtc, 0);
		set_scanout(crtc, 1);
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
	WARN_ON(!kfifo_is_empty(&lcdc_crtc->unref_fifo));
	kfifo_free(&lcdc_crtc->unref_fifo);
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

	crtc->fb = fb;
	lcdc_crtc->event = event;
	update_scanout(crtc);

	return 0;
}

static void lcdc_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct lcdc_drm_private *priv = dev->dev_private;

	/* we really only care about on or off: */
	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (lcdc_crtc->dpms == mode)
		return;

	lcdc_crtc->dpms = mode;

	pm_runtime_get_sync(dev->dev);

	if (mode == DRM_MODE_DPMS_ON) {
		pm_runtime_forbid(dev->dev);
		start(crtc);
	} else {
		lcdc_crtc->frame_done = false;
		stop(crtc);

		/* if necessary wait for framedone irq which will still come
		 * before putting things to sleep..
		 */
		if (priv->rev == 2) {
			int ret = wait_event_timeout(
					lcdc_crtc->frame_done_wq,
					lcdc_crtc->frame_done,
					msecs_to_jiffies(50));
			if (ret == 0)
				dev_err(dev->dev, "timeout waiting for framedone\n");
		}
		pm_runtime_allow(dev->dev);
	}

	pm_runtime_put_sync(dev->dev);
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
	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct lcdc_drm_private *priv = dev->dev_private;
	const struct lcdc_panel_info *info = lcdc_crtc->info;
	uint32_t reg, hbp, hfp, hsw, vbp, vfp, vsw;
	int ret;

	ret = lcdc_crtc_mode_valid(crtc, mode);
	if (WARN_ON(ret))
		return ret;

	if (WARN_ON(!info))
		return -EINVAL;

	// TODO possibly need to wait for previous DMA to complete here

	pm_runtime_get_sync(dev->dev);

	/* Configure the Burst Size and fifo threshold of DMA: */
	reg = lcdc_read(dev, LCDC_DMA_CTRL_REG) & ~0x00000770;
	switch (info->dma_burst_sz) {
	case 1:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_1);
		break;
	case 2:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_2);
		break;
	case 4:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_4);
		break;
	case 8:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_8);
		break;
	case 16:
		reg |= LCDC_DMA_BURST_SIZE(LCDC_DMA_BURST_16);
		break;
	default:
		return -EINVAL;
	}
	reg |= (info->fifo_th << 8);
	lcdc_write(dev, LCDC_DMA_CTRL_REG, reg);

	/* Configure the AC Bias Period and Number of Transitions per Interrupt: */
	reg = lcdc_read(dev, LCDC_RASTER_TIMING_2_REG) & ~0x000fff00;
	reg |= LCDC_AC_BIAS_FREQUENCY(info->ac_bias) |
		LCDC_AC_BIAS_TRANSITIONS_PER_INT(info->ac_bias_intrpt);
	lcdc_write(dev, LCDC_RASTER_TIMING_2_REG, reg);

	/* Configure timings: */
	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	DBG("%dx%d, hbp=%u, hfp=%u, hsw=%u, vbp=%u, vfp=%u, vsw=%u",
			mode->hdisplay, mode->vdisplay, hbp, hfp, hsw, vbp, vfp, vsw);

	reg = (((mode->hdisplay >> 4) - 1) << 4) |
		((hbp & 0xff) << 24) |
		((hfp & 0xff) << 16) |
		((hsw & 0x3f) << 10);
	if (priv->rev == 2)
		reg |= (((mode->hdisplay >> 4) - 1) & 0x40) >> 3;
	lcdc_write(dev, LCDC_RASTER_TIMING_0_REG, reg);

	reg = ((mode->vdisplay - 1) & 0x3ff) |
		((vbp & 0xff) << 24) |
		((vfp & 0xff) << 16) |
		((vsw & 0x3f) << 10);
	lcdc_write(dev, LCDC_RASTER_TIMING_1_REG, reg);

	/* Configure display type: */
	reg = lcdc_read(dev, LCDC_RASTER_CTRL_REG) &
		~(LCDC_TFT_MODE | LCDC_MONO_8BIT_MODE | LCDC_MONOCHROME_MODE |
			LCDC_V2_TFT_24BPP_MODE | LCDC_V2_TFT_24BPP_UNPACK | 0x000ff000);
	reg |= LCDC_TFT_MODE; /* no monochrome/passive support */
	if (info->tft_alt_mode)
		reg |= LCDC_TFT_ALT_ENABLE;
	if (priv->rev == 2) {
		unsigned int depth, bpp;

		drm_fb_get_bpp_depth(crtc->fb->pixel_format, &depth, &bpp);
		switch (bpp) {
		case 16:
			break;
		case 32:
			reg |= LCDC_V2_TFT_24BPP_UNPACK;
			/* fallthrough */
		case 24:
			reg |= LCDC_V2_TFT_24BPP_MODE;
			break;
		default:
			dev_err(dev->dev, "invalid pixel format\n");
			return -EINVAL;
		}
	}
	reg |= info->fdd < 12;
	lcdc_write(dev, LCDC_RASTER_CTRL_REG, reg);

	if (info->invert_pxl_clk)
		lcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_PIXEL_CLOCK);
	else
		lcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_PIXEL_CLOCK);

	if (info->sync_ctrl)
		lcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_CTRL);
	else
		lcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_CTRL);

	if (info->sync_edge)
		lcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_EDGE);
	else
		lcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_SYNC_EDGE);

	if (info->invert_line_clock)
		lcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_LINE_CLOCK);
	else
		lcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_LINE_CLOCK);

	if (info->invert_frm_clock)
		lcdc_set(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_FRAME_CLOCK);
	else
		lcdc_clear(dev, LCDC_RASTER_TIMING_2_REG, LCDC_INVERT_FRAME_CLOCK);

	if (info->raster_order)
		lcdc_set(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ORDER);
	else
		lcdc_clear(dev, LCDC_RASTER_CTRL_REG, LCDC_RASTER_ORDER);


	/* Set msb's of fields extended in rev 2 */
	if (priv->rev == 2) {
		// XXX TODO RASTER_TIMINGS_2 has some msb hsw/hfp/hbp..
	}

	update_scanout(crtc);
	lcdc_crtc_update_clk(crtc);

	pm_runtime_put_sync(dev->dev);

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

int lcdc_crtc_max_width(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct lcdc_drm_private *priv = dev->dev_private;
	int max_width = 0;

	if (priv->rev == 1)
		max_width = 1024;
	else if (priv->rev == 2)
		max_width = 2048;

	return max_width;
}

int lcdc_crtc_mode_valid(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	if (mode->hdisplay > lcdc_crtc_max_width(crtc))
		return MODE_VIRTUAL_X;

	/* width must be multiple of 16 */
	if (mode->hdisplay & 0xf)
		return MODE_VIRTUAL_X;

	if (mode->vdisplay > 2048)
		return MODE_VIRTUAL_Y;

	return MODE_OK;
}

void lcdc_crtc_set_panel_info(struct drm_crtc *crtc,
		const struct lcdc_panel_info *info)
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
	int ret;

	pm_runtime_get_sync(dev->dev);

	if (dpms == DRM_MODE_DPMS_ON)
		lcdc_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	/* in raster mode, minimum divisor is 2: */
	ret = clk_set_rate(priv->disp_clk, crtc->mode.clock * 1000 * 2);
	if (ret) {
		dev_err(dev->dev, "failed to set display clock rate to: %d\n",
				crtc->mode.clock);
		goto out;
	}

	lcd_clk = clk_get_rate(priv->clk);
	div = lcd_clk / (crtc->mode.clock * 1000);

	DBG("lcd_clk=%u, mode clock=%d, div=%u", lcd_clk, crtc->mode.clock, div);
	DBG("fck=%lu, dpll_disp_ck=%lu", clk_get_rate(priv->clk), clk_get_rate(priv->disp_clk));

	/* Configure the LCD clock divisor. */
	lcdc_write(dev, LCDC_CTRL_REG, LCDC_CLK_DIVISOR(div) |
			LCDC_RASTER_MODE);

	if (priv->rev == 2)
		lcdc_set(dev, LCDC_CLK_ENABLE_REG,
				LCDC_V2_DMA_CLK_EN | LCDC_V2_LIDD_CLK_EN |
				LCDC_V2_CORE_CLK_EN);

	if (dpms == DRM_MODE_DPMS_ON)
		lcdc_crtc_dpms(crtc, DRM_MODE_DPMS_ON);

out:
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
		dev_err(dev->dev, "error: %08x\n", stat);
		lcdc_clear_irqstatus(dev, stat);
		start(crtc);
	} else if (stat & LCDC_PL_LOAD_DONE) {
		/* TODO lut load.. */
		lcdc_clear_irqstatus(dev, stat);
	} else {
		struct drm_pending_vblank_event *event;
		unsigned long flags;
		uint32_t dirty = lcdc_crtc->dirty & stat;

		lcdc_clear_irqstatus(dev, stat);

		if (dirty & LCDC_END_OF_FRAME0)
			set_scanout(crtc, 0);

		if (dirty & LCDC_END_OF_FRAME1)
			set_scanout(crtc, 1);

		drm_handle_vblank(dev, 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		event = lcdc_crtc->event;
		lcdc_crtc->event = NULL;
		if (event)
			drm_send_vblank_event(dev, 0, event);
		spin_unlock_irqrestore(&dev->event_lock, flags);

		if (dirty && !lcdc_crtc->dirty)
			drm_vblank_put(dev, 0);
	}

	if (priv->rev == 2) {
		if (stat & LCDC_FRAME_DONE) {
			lcdc_crtc->frame_done = true;
			wake_up(&lcdc_crtc->frame_done_wq);
		}
		lcdc_write(dev, LCDC_END_OF_INT_IND_REG, 0);
	}

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

	crtc = &lcdc_crtc->base;

	lcdc_crtc->dpms = DRM_MODE_DPMS_OFF;
	init_waitqueue_head(&lcdc_crtc->frame_done_wq);

	ret = kfifo_alloc(&lcdc_crtc->unref_fifo, 16, GFP_KERNEL);
	if (ret) {
		dev_err(dev->dev, "could not allocate unref FIFO\n");
		goto fail;
	}

	INIT_WORK(&lcdc_crtc->work, unref_worker);

	ret = drm_crtc_init(dev, crtc, &lcdc_crtc_funcs);
	if (ret < 0)
		goto fail;

	drm_crtc_helper_add(crtc, &lcdc_crtc_helper_funcs);

	return crtc;

fail:
	lcdc_crtc_destroy(crtc);
	return NULL;
}
