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

struct lcdc_crtc {
	struct drm_crtc base;

	struct drm_pending_vblank_event *event;
	int dpms;
};
#define to_lcdc_crtc(x) container_of(x, struct lcdc_crtc, base)


static void lcdc_crtc_destroy(struct drm_crtc *crtc)
{
	// XXX
	drm_crtc_cleanup(crtc);
}

static int lcdc_crtc_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *fb,
		struct drm_pending_vblank_event *event)
{
	// XXX
	return -1;
}

static void lcdc_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	// XXX
}

static bool lcdc_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	// XXX
	return false;
}

static void lcdc_crtc_prepare(struct drm_crtc *crtc)
{
	// XXX
}

static void lcdc_crtc_commit(struct drm_crtc *crtc)
{
	// XXX
}

static int lcdc_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	// XXX
	return -1;
}

static int lcdc_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	// XXX
	return -1;
}

static void lcdc_crtc_load_lut(struct drm_crtc *crtc)
{
	// XXX
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

void lcdc_crtc_finish_page_flip(struct drm_crtc *crtc)
{
//	struct lcdc_crtc *lcdc_crtc = to_lcdc_crtc(crtc);

	// TODO
}

void lcdc_crtc_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	// TODO
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
