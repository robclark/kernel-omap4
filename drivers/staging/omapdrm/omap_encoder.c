/*
 * drivers/staging/omapdrm/omap_encoder.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
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

#include "omap_drv.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#include <linux/list.h>


/*
 * encoder funcs
 */

#define to_omap_encoder(x) container_of(x, struct omap_encoder, base)

struct omap_encoder {
	struct drm_encoder base;

	/*
	 * Currently this is just a stub.. but when we get rid of
	 * omap_dss_device in the connector, what we *should* do
	 * is have the drm encoder map to the what the dssdev is
	 * now (ie. DSI1, DSI2, RFBI, VENC, HDMI), and then have
	 * the connector map to the panel driver
	 *
	 * Instead for now we just create one encoder for each
	 * connector which is statically bound to that connector.
	 */
};

static void omap_encoder_destroy(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(omap_encoder);
}

static void omap_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static bool omap_encoder_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
}

static void omap_encoder_prepare(struct drm_encoder *encoder)
{
}

static void omap_encoder_commit(struct drm_encoder *encoder)
{
}

static const struct drm_encoder_funcs omap_encoder_funcs = {
	.destroy = omap_encoder_destroy,
};

static const struct drm_encoder_helper_funcs omap_encoder_helper_funcs = {
	.dpms = omap_encoder_dpms,
	.mode_fixup = omap_encoder_mode_fixup,
	.mode_set = omap_encoder_mode_set,
	.prepare = omap_encoder_prepare,
	.commit = omap_encoder_commit,
};

/* initialize encoder */
struct drm_encoder *omap_encoder_init(struct drm_device *dev)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder) {
		dev_err(dev->dev, "could not allocate encoder\n");
		goto fail;
	}

	encoder = &omap_encoder->base;

	drm_encoder_init(dev, encoder, &omap_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &omap_encoder_helper_funcs);

	return encoder;

fail:
	if (encoder) {
		omap_encoder_destroy(encoder);
	}

	return NULL;
}
