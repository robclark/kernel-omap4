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
	struct omap_overlay_manager *mgr; // TODO remove

	const char *name;
	enum omap_channel channel;
	struct omap_overlay_manager_info info;

	struct omap_video_timings timings;
	bool enabled, timings_valid;
	uint32_t irqmask;

	struct omap_drm_apply apply;

	/* list of in-progress apply's: */
	struct list_head pending_applies;

	/* list of queued apply's: */
	struct list_head queued_applies;

	/* for handling queued and in-progress applies: */
	struct work_struct work;
};

static void omap_encoder_destroy(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	DBG("%s", omap_encoder->name);
	drm_encoder_cleanup(encoder);
	kfree(omap_encoder);
}

static void omap_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	bool enabled = (mode == DRM_MODE_DPMS_ON);

	DBG("%s: %d", omap_encoder->name, mode);

	if (enabled != omap_encoder->enabled) {
		omap_encoder->enabled = enabled;
		omap_encoder_apply(encoder, &omap_encoder->apply);
	}
}

static bool omap_encoder_mode_fixup(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	DBG("%s", omap_encoder->name);
	return true;
}

static void omap_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct omap_drm_private *priv = dev->dev_private;
	int i;

	mode = adjusted_mode;

	DBG("%s: set mode: %dx%d", omap_encoder->name,
			mode->hdisplay, mode->vdisplay);

	for (i = 0; i < priv->num_connectors; i++) {
		struct drm_connector *connector = priv->connectors[i];
		if (connector->encoder == encoder) {
			omap_encoder->timings_valid = omap_connector_mode_set(
					connector, mode, &omap_encoder->timings) == 0;
		}
	}
}

static void omap_encoder_prepare(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_encoder_helper_funcs *encoder_funcs =
				encoder->helper_private;
	DBG("%s", omap_encoder->name);
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void omap_encoder_commit(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_encoder_helper_funcs *encoder_funcs =
				encoder->helper_private;
	DBG("%s", omap_encoder->name);
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
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

static void omap_encoder_pre_apply(struct omap_drm_apply *apply)
{
	struct omap_encoder *omap_encoder =
			container_of(apply, struct omap_encoder, apply);

	DBG("%s: enabled=%d, timings_valid=%d", omap_encoder->name,
			omap_encoder->enabled,
			omap_encoder->timings_valid);

	if (!omap_encoder->enabled || !omap_encoder->timings_valid) {
		dispc_mgr_enable(omap_encoder->channel, false);
		return;
	}

	dispc_mgr_setup(omap_encoder->channel, &omap_encoder->info);
	dispc_mgr_set_timings(omap_encoder->channel,
			&omap_encoder->timings);
	dispc_mgr_enable(omap_encoder->channel, true);
}

static void omap_encoder_post_apply(struct omap_drm_apply *apply)
{
	/* nothing needed for post-apply */
}

// TODO remove
struct omap_overlay_manager *omap_encoder_get_manager(
		struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	return omap_encoder->mgr;
}

const struct omap_video_timings *omap_encoder_timings(
		struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	return &omap_encoder->timings;
}

enum omap_channel omap_encoder_channel(struct drm_encoder *encoder)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	return omap_encoder->channel;
}

void omap_encoder_vblank(struct drm_encoder *encoder, uint32_t irqstatus)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);

	if (irqstatus & omap_encoder->irqmask) {
		DBG("%s: it's for me!", omap_encoder->name);
		if (!dispc_mgr_go_busy(omap_encoder->channel)) {
			struct omap_drm_private *priv =
					encoder->dev->dev_private;
			DBG("%s: apply done", omap_encoder->name);
			omap_irq_disable(omap_encoder->irqmask);
			queue_work(priv->wq, &omap_encoder->work);
		}
	}
}

static void apply_worker(struct work_struct *work)
{
	struct omap_encoder *omap_encoder =
			container_of(work, struct omap_encoder, work);
	struct drm_encoder *encoder = &omap_encoder->base;
	struct drm_device *dev = encoder->dev;
	struct omap_drm_apply *apply, *n;
	bool need_apply;

	/*
	 * Synchronize everything on mode_config.mutex, to keep
	 * the callbacks and list modification all serialized
	 * with respect to modesetting ioctls from userspace.
	 */
	mutex_lock(&dev->mode_config.mutex);
	dispc_runtime_get();

	/* finish up previous apply's: */
	list_for_each_entry_safe(apply, n,
			&omap_encoder->pending_applies, pending_node) {
		apply->post_apply(apply);
		list_del(&apply->pending_node);
	}

	need_apply = !list_empty(&omap_encoder->queued_applies);

	/* then handle the next round of of queued apply's: */
	list_for_each_entry_safe(apply, n,
			&omap_encoder->queued_applies, queued_node) {
		apply->pre_apply(apply);
		list_del(&apply->queued_node);
		apply->queued = false;
		list_add_tail(&apply->pending_node,
				&omap_encoder->pending_applies);
	}

	if (need_apply) {
		DBG("%s: GO", omap_encoder->name);
		omap_irq_enable(omap_encoder->irqmask);
		dispc_mgr_go(omap_encoder->channel);
	}
	dispc_runtime_put();
	mutex_unlock(&dev->mode_config.mutex);
}

int omap_encoder_apply(struct drm_encoder *encoder,
		struct omap_drm_apply *apply)
{
	struct omap_encoder *omap_encoder = to_omap_encoder(encoder);
	struct drm_device *dev = encoder->dev;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));

	/* no need to queue it again if it is already queued: */
	if (apply->queued)
		return 0;

	apply->queued = true;
	list_add_tail(&apply->queued_node, &omap_encoder->queued_applies);

	/*
	 * If there are no currently pending updates, then go ahead and
	 * kick the worker immediately, otherwise it will run again when
	 * the current update finishes.
	 */
	if (list_empty(&omap_encoder->pending_applies)) {
		struct omap_drm_private *priv = encoder->dev->dev_private;
		queue_work(priv->wq, &omap_encoder->work);
	}

	return 0;
}

/* initialize encoder */
struct drm_encoder *omap_encoder_init(struct drm_device *dev,
		struct omap_overlay_manager *mgr)
{
	struct drm_encoder *encoder = NULL;
	struct omap_encoder *omap_encoder;
	struct omap_overlay_manager_info *info;

	DBG("%s", mgr->name);

	omap_encoder = kzalloc(sizeof(*omap_encoder), GFP_KERNEL);
	if (!omap_encoder) {
		dev_err(dev->dev, "could not allocate encoder\n");
		goto fail;
	}

	omap_encoder->name = mgr->name;
	omap_encoder->channel = mgr->id;
	omap_encoder->irqmask =
			dispc_mgr_get_vsync_irq(mgr->id);
	omap_encoder->mgr = mgr;

	encoder = &omap_encoder->base;

	INIT_WORK(&omap_encoder->work, apply_worker);
	INIT_LIST_HEAD(&omap_encoder->pending_applies);
	INIT_LIST_HEAD(&omap_encoder->queued_applies);

	omap_encoder->apply.pre_apply  = omap_encoder_pre_apply;
	omap_encoder->apply.post_apply = omap_encoder_post_apply;

	drm_encoder_init(dev, encoder, &omap_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &omap_encoder_helper_funcs);

	info = &omap_encoder->info;
	mgr->get_manager_info(mgr, info);

	/* TODO: fix hard-coded setup.. add properties! */
	info->default_color = 0x00000000;
	info->trans_key = 0x00000000;
	info->trans_key_type = OMAP_DSS_COLOR_KEY_GFX_DST;
	info->trans_enabled = false;

	return encoder;

fail:
	if (encoder) {
		omap_encoder_destroy(encoder);
	}

	return NULL;
}
