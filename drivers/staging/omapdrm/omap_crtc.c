/*
 * drivers/staging/omapdrm/omap_crtc.c
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

#include "drm_mode.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#define to_omap_crtc(x) container_of(x, struct omap_crtc, base)

struct omap_crtc {
	struct drm_crtc base;
	struct drm_plane *plane;

	const char *name;
	int pipe;
	enum omap_channel channel;

	struct omap_video_timings timings;
	bool enabled, timings_valid;

	struct omap_drm_apply apply;
	struct omap_drm_irq irq;

	/* list of in-progress apply's: */
	struct list_head pending_applies;

	/* list of queued apply's: */
	struct list_head queued_applies;

	bool in_apply;       /* for debug */

	/* for handling queued and in-progress applies: */
	struct work_struct apply_work;
};

static int omap_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb);

static void omap_crtc_destroy(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	omap_crtc->plane->funcs->destroy(omap_crtc->plane);
	drm_crtc_cleanup(crtc);
	kfree(crtc->state);
	kfree(omap_crtc);
}

static void omap_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	bool enabled = (mode == DRM_MODE_DPMS_ON);
	int i;

	DBG("%s: %d", omap_crtc->name, mode);

	if (enabled != omap_crtc->enabled) {
		omap_crtc->enabled = enabled;
		omap_crtc_apply(crtc, &omap_crtc->apply);

		/* also enable our private plane: */
		WARN_ON(omap_plane_dpms(omap_crtc->plane, mode));

		/* and any attached overlay planes: */
		for (i = 0; i < priv->num_planes; i++) {
			struct drm_plane *plane = priv->planes[i];
			if (plane->state->crtc == crtc)
				WARN_ON(omap_plane_dpms(plane, mode));
		}
	}
}

static bool omap_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int omap_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct omap_drm_private *priv = dev->dev_private;
	int i;

	mode = adjusted_mode;

	for (i = 0; i < priv->num_connectors; i++) {
		struct drm_connector *connector =
				priv->connectors[i];
		struct drm_encoder *encoder =
				omap_connector_attached_encoder(connector);
		if (crtc == encoder->crtc) {
			int ret = omap_connector_mode_set(connector,
					mode, &omap_crtc->timings);
			omap_crtc->timings_valid = (ret == 0);
		}
	}

	return omap_crtc_mode_set_base(crtc, x, y, old_fb);
}

static void omap_crtc_prepare(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void omap_crtc_commit(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int omap_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_plane *plane = omap_crtc->plane;
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_display_mode *mode = &crtc->mode;
	void *state;
	int ret;

	/* for now, until property based atomic mode-set: */
	state = omap_atomic_begin(dev, NULL);
	if (IS_ERR(state))
		return PTR_ERR(state);

	ret =
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_crtc_id, crtc->base.id) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_fb_id, crtc->state->fb->base.id) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_crtc_x, 0) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_crtc_y, 0) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_crtc_w, mode->hdisplay) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_crtc_h, mode->vdisplay) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_src_w, mode->hdisplay << 16) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_src_h, mode->vdisplay << 16) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_src_x, x << 16) ||
		drm_mode_plane_set_obj_prop(plane, state,
				config->prop_src_y, y << 16) ||
		dev->driver->atomic_check(dev, state);

	if (!ret)
		ret = omap_atomic_commit(dev, state, NULL);

	omap_atomic_end(dev, state);

	return ret;
}

static void omap_crtc_load_lut(struct drm_crtc *crtc)
{
}

static int omap_crtc_set_property(struct drm_crtc *crtc, void *state,
		struct drm_property *property, uint64_t val)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc_state *crtc_state =
			omap_atomic_crtc_state(state, omap_crtc->pipe);
	int ret;

	DBG("%s: %s = %llx", omap_crtc->name, property->name, val);

	ret = drm_crtc_set_property(crtc, &crtc_state->base, property, val);
	if (!ret) {
		/* we need to set fb property on our private plane too:
		 */
		struct drm_mode_config *config = &crtc->dev->mode_config;
		if (property != config->prop_fb_id)
			return ret;
	}

	if (property == priv->rotation_prop) {
		crtc_state->base.invert_dimensions =
				!!(val & ((1LL << DRM_ROTATE_90) | (1LL << DRM_ROTATE_270)));
	}

	return omap_plane_set_property(omap_crtc->plane, state, property, val);
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = omap_crtc_destroy,
	.set_property = omap_crtc_set_property,
};

static const struct drm_crtc_helper_funcs omap_crtc_helper_funcs = {
	.dpms = omap_crtc_dpms,
	.mode_fixup = omap_crtc_mode_fixup,
	.mode_set = omap_crtc_mode_set,
	.prepare = omap_crtc_prepare,
	.commit = omap_crtc_commit,
	.mode_set_base = omap_crtc_mode_set_base,
	.load_lut = omap_crtc_load_lut,
};

const struct omap_video_timings *omap_crtc_timings(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return &omap_crtc->timings;
}

enum omap_channel omap_crtc_channel(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return omap_crtc->channel;
}

int omap_crtc_check_state(struct drm_crtc *crtc,
		struct omap_crtc_state *crtc_state)
{
	return drm_crtc_check_state(crtc, &crtc_state->base);
}

void omap_crtc_commit_state(struct drm_crtc *crtc,
		struct omap_crtc_state *crtc_state)
{

	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_crtc_state *old_state = to_omap_crtc_state(crtc->state);
	DBG("%s", omap_crtc->name);
	drm_crtc_commit_state(crtc, &crtc_state->base);
	kfree(old_state);
	omap_crtc_apply(crtc, &omap_crtc->apply);
}

static void omap_crtc_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc =
			container_of(irq, struct omap_crtc, irq);
	struct drm_crtc *crtc = &omap_crtc->base;

	if (!dispc_mgr_go_busy(omap_crtc->channel)) {
		struct omap_drm_private *priv =
				crtc->dev->dev_private;
		DBG("%s: apply done", omap_crtc->name);
		omap_irq_unregister(crtc->dev, &omap_crtc->irq);
		queue_work(priv->wq, &omap_crtc->apply_work);
	}
}

static void apply_worker(struct work_struct *work)
{
	struct omap_crtc *omap_crtc =
			container_of(work, struct omap_crtc, apply_work);
	struct drm_crtc *crtc = &omap_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct omap_drm_apply *apply, *n;
	bool need_apply;

	/*
	 * Synchronize everything on mode_config.mutex, to keep
	 * the callbacks and list modification all serialized
	 * with respect to modesetting ioctls from userspace.
	 */
	mutex_lock(&dev->mode_config.mutex);
	omap_crtc->in_apply = true;
	dispc_runtime_get();

	/*
	 * If we are still pending a previous update, wait.. when the
	 * pending update completes, we get kicked again.
	 */
	if (omap_crtc->irq.registered)
		goto out;

	/* finish up previous apply's: */
	list_for_each_entry_safe(apply, n,
			&omap_crtc->pending_applies, pending_node) {
		apply->post_apply(apply);
		list_del(&apply->pending_node);
	}

	if (pipe_in_atomic(dev, omap_crtc->pipe))
		goto out;

	need_apply = !list_empty(&omap_crtc->queued_applies);

	/* then handle the next round of of queued apply's: */
	list_for_each_entry_safe(apply, n,
			&omap_crtc->queued_applies, queued_node) {
		apply->pre_apply(apply);
		list_del(&apply->queued_node);
		apply->queued = false;
		list_add_tail(&apply->pending_node,
				&omap_crtc->pending_applies);
	}

	if (need_apply) {
		DBG("%s: GO", omap_crtc->name);
		omap_irq_register(dev, &omap_crtc->irq);
		if (!dispc_mgr_go(omap_crtc->channel)) {
			/* if display is disabled, or dispc otherwise didn't
			 * set the GO bit, then no use to wait for an irq
			 * that will never come.
			 */
			omap_crtc_irq(&omap_crtc->irq, 0);
		}
	}

out:
	dispc_runtime_put();
	omap_crtc->in_apply = false;
	mutex_unlock(&dev->mode_config.mutex);
}

int omap_crtc_apply(struct drm_crtc *crtc,
		struct omap_drm_apply *apply)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = crtc->dev;

	WARN_ON(!mutex_is_locked(&dev->mode_config.mutex));
	WARN_ON(omap_crtc->in_apply);

	/* no need to queue it again if it is already queued: */
	if (! apply->queued) {
		apply->queued = true;
		list_add_tail(&apply->queued_node,
				&omap_crtc->queued_applies);
	}

	if (pipe_in_atomic(dev, omap_crtc->pipe))
		return 0;

	/*
	 * If there are no currently pending updates, then go ahead and
	 * kick the worker immediately, otherwise it will run again when
	 * the current update finishes.
	 */
	if (list_empty(&omap_crtc->pending_applies)) {
		struct omap_drm_private *priv = crtc->dev->dev_private;
		queue_work(priv->wq, &omap_crtc->apply_work);
	}

	return 0;
}

static void omap_crtc_pre_apply(struct omap_drm_apply *apply)
{
	struct omap_crtc *omap_crtc =
			container_of(apply, struct omap_crtc, apply);
	struct omap_overlay_manager_info info = {0};

	DBG("%s: enabled=%d, timings_valid=%d", omap_crtc->name,
			omap_crtc->enabled,
			omap_crtc->timings_valid);

	if (!omap_crtc->enabled || !omap_crtc->timings_valid) {
		dispc_mgr_enable(omap_crtc->channel, false);
		return;
	}

	info.default_color = 0x00000000;
	info.trans_key = 0x00000000;
	info.trans_key_type = OMAP_DSS_COLOR_KEY_GFX_DST;
	info.trans_enabled = false;

	dispc_mgr_setup(omap_crtc->channel, &info);
	dispc_mgr_set_timings(omap_crtc->channel,
			&omap_crtc->timings);
	dispc_mgr_enable(omap_crtc->channel, true);
}

static void omap_crtc_post_apply(struct omap_drm_apply *apply)
{
	/* nothing needed for post-apply */
}

static const char *channel_names[] = {
		[OMAP_DSS_CHANNEL_LCD] = "lcd",
		[OMAP_DSS_CHANNEL_DIGIT] = "tv",
		[OMAP_DSS_CHANNEL_LCD2] = "lcd2",
};

/* initialize crtc */
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, enum omap_channel channel, int id)
{
	struct drm_crtc *crtc = NULL;
	struct omap_crtc *omap_crtc;

	DBG("%s", channel_names[channel]);

	omap_crtc = kzalloc(sizeof(*omap_crtc), GFP_KERNEL);

	if (!omap_crtc) {
		dev_err(dev->dev, "could not allocate CRTC\n");
		goto fail;
	}

	crtc = &omap_crtc->base;

	crtc->state = kzalloc(sizeof(struct omap_crtc_state), GFP_KERNEL);

	if (!crtc->state) {
		dev_err(dev->dev, "could not allocate CRTC state\n");
		goto fail;
	}

	INIT_WORK(&omap_crtc->apply_work, apply_worker);

	INIT_LIST_HEAD(&omap_crtc->pending_applies);
	INIT_LIST_HEAD(&omap_crtc->queued_applies);

	omap_crtc->apply.pre_apply  = omap_crtc_pre_apply;
	omap_crtc->apply.post_apply = omap_crtc_post_apply;

	omap_crtc->irq.irqmask = pipe2vbl(id);
	omap_crtc->irq.irq = omap_crtc_irq;

	omap_crtc->channel = channel;
	omap_crtc->plane = plane;
	omap_crtc->plane->state->crtc = crtc;
	omap_crtc->name = channel_names[channel];
	omap_crtc->pipe = id;

	drm_crtc_init(dev, crtc, &omap_crtc_funcs);
	drm_crtc_helper_add(crtc, &omap_crtc_helper_funcs);

	omap_plane_install_properties(omap_crtc->plane, &crtc->base);

	return crtc;

fail:
	if (crtc) {
		omap_crtc_destroy(crtc);
	}
	return NULL;
}
