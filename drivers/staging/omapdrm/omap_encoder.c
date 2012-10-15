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

/* The encoder and connector both map to same dssdev.. the encoder
 * handles the 'active' parts, ie. anything the modifies the state
 * of the hw, and the connector handles the 'read-only' parts, like
 * detecting connection and reading edid.
 */
struct omap_encoder {
	struct drm_encoder base;
	struct omap_dss_device *dssdev;
	struct omap_drm_apply apply;
	bool enabled;
	struct omap_video_timings timings;
};

static void omap_encoder_pre_apply(struct omap_drm_apply *apply)
{
	struct omap_encoder *omap_encoder =
			container_of(apply, struct omap_encoder, apply);
	struct omap_dss_device *dssdev = omap_encoder->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;

	DBG("%s: enabled=%d", dssdev->name, omap_encoder->enabled);

	if (omap_encoder->enabled) {
		dssdrv->set_timings(dssdev, &omap_encoder->timings);
		dssdrv->enable(dssdev);
	} else {
		dssdrv->disable(dssdev);
	}
}

static void omap_encoder_post_apply(struct omap_drm_apply *apply)
{
	/* nothing needed */
}

static void omap_encoder_destroy(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(omap_encoder);
}

static void omap_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	omap_encoder->enabled = (mode == DRM_MODE_DPMS_ON);
	DBG("%s: %d", omap_encoder->dssdev->name, mode);
	if (encoder->crtc)
		omap_crtc_apply(encoder->crtc, &omap_encoder->apply);
}

static bool omap_encoder_mode_fixup(struct drm_encoder *encoder,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

/* called from encoder when mode is set, to propagate settings to the dssdev */
int omap_encoder_set_timings(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct omap_video_timings *timings)
{
	struct drm_device *dev = encoder->dev;
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct omap_dss_device *dssdev = omap_encoder->dssdev;
	struct omap_dss_driver *dssdrv = dssdev->driver;
	int ret;

	copy_timings_drm_to_omap(timings, mode);

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			dssdev->name, mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	if (encoder->crtc)
		dssdev->output->manager_id = omap_crtc_channel(encoder->crtc);

	ret = dssdrv->check_timings(dssdev, timings);
	if (ret) {
		dev_err(dev->dev, "could not set timings: %d\n", ret);
		return ret;
	}

	omap_encoder->timings = *timings;
	omap_encoder->enabled = true;

	if (encoder->crtc)
		omap_crtc_apply(encoder->crtc, &omap_encoder->apply);

	return 0;
}

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct omap_video_timings timings = {0};
	omap_encoder_set_timings(encoder, mode, &timings);
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
struct drm_encoder *omap_encoder_init(struct drm_device *dev,
		struct omap_dss_device *dssdev)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder) {
		dev_err(dev->dev, "could not allocate encoder\n");
		goto fail;
	}

	omap_encoder->dssdev = dssdev;

	omap_encoder->apply.pre_apply  = omap_encoder_pre_apply;
	omap_encoder->apply.post_apply = omap_encoder_post_apply;

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
