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

struct lcdc_encoder {
	struct drm_encoder base;
	int dpms;
};
#define to_lcdc_encoder(x) container_of(x, struct lcdc_encoder, base)


static void lcdc_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	// XXX
}

static void lcdc_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	// XXX
}

static bool lcdc_encoder_mode_fixup(struct drm_encoder *encoder,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	// XXX
	return false;
}

static void lcdc_encoder_prepare(struct drm_encoder *encoder)
{
	// XXX
}

static void lcdc_encoder_commit(struct drm_encoder *encoder)
{
	// XXX
}

static void lcdc_encoder_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	// XXX
}

static const struct drm_encoder_funcs lcdc_encoder_funcs = {
		.destroy        = lcdc_encoder_destroy,
};

static const struct drm_encoder_helper_funcs lcdc_encoder_helper_funcs = {
		.dpms           = lcdc_encoder_dpms,
		.mode_fixup     = lcdc_encoder_mode_fixup,
		.prepare        = lcdc_encoder_prepare,
		.commit         = lcdc_encoder_commit,
		.mode_set       = lcdc_encoder_mode_set,
};

struct drm_encoder *lcdc_encoder_create(struct drm_device *dev)
{
	struct lcdc_encoder *lcdc_encoder;
	struct drm_encoder *encoder;
	int ret;

	lcdc_encoder = kzalloc(sizeof(*lcdc_encoder), GFP_KERNEL);
	if (!lcdc_encoder) {
		dev_err(dev->dev, "allocation failed\n");
		return NULL;
	}

	lcdc_encoder->dpms = DRM_MODE_DPMS_OFF;
	encoder = &lcdc_encoder->base;

	ret = drm_encoder_init(dev, encoder, &lcdc_encoder_funcs,
			DRM_MODE_ENCODER_LVDS);
	if (ret < 0)
		goto fail;

	drm_encoder_helper_add(encoder, &lcdc_encoder_helper_funcs);

	return encoder;

fail:
	lcdc_encoder_destroy(encoder);
	return NULL;
}
