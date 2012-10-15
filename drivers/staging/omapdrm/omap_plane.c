/*
 * drivers/staging/omapdrm/omap_plane.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
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

#include "omap_drv.h"
#include "omap_dmm_tiler.h"

/* some hackery because omapdss has an 'enum omap_plane' (which would be
 * better named omap_plane_id).. and compiler seems unhappy about having
 * both a 'struct omap_plane' and 'enum omap_plane'
 */
#define omap_plane _omap_plane

/*
 * plane funcs
 */

#define to_omap_plane(x) container_of(x, struct omap_plane, base)

struct omap_plane {
	struct drm_plane base;
	int id;  /* TODO rename omap_plane -> omap_plane_id in omapdss so I can use the enum */
	const char *name;
	struct omap_drm_apply apply;

	/* last fb that we pinned: */
	struct drm_framebuffer *pinned_fb;

	uint32_t nformats;
	uint32_t formats[32];

	struct omap_drm_irq error_irq;

	/* set of bo's pending unpin until next post_apply() */
	DECLARE_KFIFO_PTR(unpin_fifo, struct drm_gem_object *);
};

static void unpin(void *arg, struct drm_gem_object *bo)
{
	struct drm_plane *plane = arg;
	struct omap_plane *omap_plane = to_omap_plane(plane);

	if (kfifo_put(&omap_plane->unpin_fifo,
			(const struct drm_gem_object **)&bo)) {
		/* also hold a ref so it isn't free'd while pinned */
		drm_gem_object_reference(bo);
	} else {
		dev_err(plane->dev->dev, "unpin fifo full!\n");
		omap_gem_put_paddr(bo);
	}
}

/* update which fb (if any) is pinned for scanout */
static int update_pin(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_framebuffer *pinned_fb = omap_plane->pinned_fb;

	if (pinned_fb != fb) {
		int ret;

		DBG("%p -> %p", pinned_fb, fb);

		if (fb)
			drm_framebuffer_reference(fb);

		ret = omap_framebuffer_replace(pinned_fb, fb, plane, unpin);

		if (pinned_fb)
			drm_framebuffer_unreference(pinned_fb);

		if (ret) {
			dev_err(plane->dev->dev, "could not swap %p -> %p\n",
					omap_plane->pinned_fb, fb);
			if (fb)
				drm_framebuffer_unreference(fb);
			omap_plane->pinned_fb = NULL;
			return ret;
		}

		omap_plane->pinned_fb = fb;
	}

	return 0;
}

static inline bool is_enabled(struct drm_plane_state *state)
{
	return to_omap_plane_state(state)->enabled &&
			state->crtc && state->fb;
}

/* TODO get rid of this and convert dispc code to use drm state
 * structs directly..
 */
static void state2info(struct omap_plane_state *plane_state,
		struct omap_overlay_info *info)
{

	memset(info, 0, sizeof(*info));

	info->global_alpha = 0xff;
	/* TODO: we should calculate valid zorder from all the planes: */
	info->zorder = plane_state->zorder;

	/* update scanout: */
	omap_framebuffer_update_scanout(plane_state->base.fb,
			&plane_state->base, info);

	DBG("%dx%d -> %dx%d (%d)", info->width, info->height,
			info->out_width, info->out_height, info->screen_width);
	DBG("%d,%d %08x %08x", info->pos_x, info->pos_y,
			info->paddr, info->p_uv_addr);
}

static void omap_plane_pre_apply(struct omap_drm_apply *apply)
{
	struct omap_plane *omap_plane =
			container_of(apply, struct omap_plane, apply);
	struct drm_plane *plane = &omap_plane->base;
	struct drm_device *dev = plane->dev;
	struct omap_overlay_info info;
	struct drm_crtc *crtc = plane->state->crtc;
	struct drm_framebuffer *fb = plane->state->fb;
	enum omap_channel channel;
	bool enabled = is_enabled(plane->state);
	bool replication;
	int ret;

	DBG("%s, enabled=%d", omap_plane->name, enabled);

	/* if fb has changed, pin new fb: */
	update_pin(plane, enabled ? fb : NULL);

	if (!enabled) {
		dispc_ovl_enable(omap_plane->id, false);
		return;
	}

	channel = omap_crtc_channel(crtc);

	state2info(to_omap_plane_state(plane->state), &info);

	/* TODO: */
	replication = false;

	/* and finally, update omapdss: */
	dispc_ovl_set_channel_out(omap_plane->id, channel);
	ret = dispc_ovl_setup(omap_plane->id, &info,
			replication, omap_crtc_timings(crtc), false);
	if (ret) {
		dev_err(dev->dev, "dispc_ovl_setup failed: %d\n", ret);
		return;
	}

	dispc_ovl_enable(omap_plane->id, true);
}

static void omap_plane_post_apply(struct omap_drm_apply *apply)
{
	struct omap_plane *omap_plane =
			container_of(apply, struct omap_plane, apply);
	struct drm_plane *plane = &omap_plane->base;
	struct drm_gem_object *bo = NULL;

	while (kfifo_get(&omap_plane->unpin_fifo, &bo)) {
		omap_gem_put_paddr(bo);
		drm_gem_object_unreference_unlocked(bo);
	}

	omap_atomic_plane_update(plane->dev, omap_plane->id);

	if (is_enabled(plane->state)) {
		struct drm_plane_state *state = plane->state;
		omap_framebuffer_flush(plane->state->fb,
				state->crtc_x, state->crtc_y,
				state->crtc_w, state->crtc_h);
	}
}

static void omap_plane_destroy(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	DBG("%s", omap_plane->name);

	omap_irq_unregister(plane->dev, &omap_plane->error_irq);

	omap_plane_dpms(plane, DRM_MODE_DPMS_OFF);
	drm_plane_cleanup(plane);

	WARN_ON(!kfifo_is_empty(&omap_plane->unpin_fifo));
	kfifo_free(&omap_plane->unpin_fifo);

	kfree(plane->state);
	kfree(omap_plane);
}

int omap_plane_dpms(struct drm_plane *plane, int mode)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_plane_state *state = plane->state;
	bool enabled = (mode == DRM_MODE_DPMS_ON);
	int ret = 0;

	DBG("%s: mode=%d", omap_plane->name, mode);

	if (enabled != is_enabled(state)) {
		to_omap_plane_state(state)->enabled = enabled;
		if (state->crtc)
			ret = omap_crtc_apply(state->crtc, &omap_plane->apply);
	}

	return ret;
}

/* helper to install properties which are common to planes and crtcs */
void omap_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj)
{
	struct drm_device *dev = plane->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_property *prop;

	prop = priv->rotation_prop;
	if (!prop) {
		const struct drm_prop_enum_list props[] = {
				{ DRM_ROTATE_0,   "rotate-0" },
				{ DRM_ROTATE_90,  "rotate-90" },
				{ DRM_ROTATE_180, "rotate-180" },
				{ DRM_ROTATE_270, "rotate-270" },
				{ DRM_REFLECT_X,  "reflect-x" },
				{ DRM_REFLECT_Y,  "reflect-y" },
		};
		prop = drm_property_create_bitmask(dev, 0, "rotation",
				props, ARRAY_SIZE(props));
		if (prop == NULL)
			return;
		priv->rotation_prop = prop;
	}
	drm_object_attach_property(obj, prop, 0);

	prop = priv->zorder_prop;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zorder", 0, 3);
		if (prop == NULL)
			return;
		priv->zorder_prop = prop;
	}
	drm_object_attach_property(obj, prop, 0);
}

int omap_plane_set_property(struct drm_plane *plane, void *state,
		struct drm_property *property, uint64_t val)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct omap_plane_state *plane_state =
			omap_atomic_plane_state(state, omap_plane->id);
	int ret;

	DBG("%s: %s = %llx", omap_plane->name, property->name, val);

	ret = drm_plane_set_property(plane, &plane_state->base, property, val);
	if (!ret) {
		/* if this property is handled by base, we are nearly done..
		 * we just need to register an fb property w/ atomic so that
		 * commit can be deferred until the fb is ready
		 */
		struct drm_mode_config *config = &plane->dev->mode_config;
		if ((property == config->prop_fb_id) && val) {
			struct drm_mode_object *obj =
					drm_property_get_obj(property, val);
			omap_atomic_add_fb(state, obj_to_fb(obj));
		}
		return ret;
	}

	/* if it is not a base plane property, see if it is one of ours: */

	if (property == priv->rotation_prop) {
		plane_state->rotation = val;
	} else if (property == priv->zorder_prop) {
		plane_state->zorder = val;
	} else {
		return -EINVAL;
	}

	return 0;
}

int omap_plane_check_state(struct drm_plane *plane,
		struct omap_plane_state *plane_state)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_crtc *crtc = plane_state->base.crtc;
	struct omap_overlay_info info;
	int ret, x_predecim = 0, y_predecim = 0;

	if (!is_enabled(&plane_state->base))
		return 0;

	ret = drm_plane_check_state(plane, &plane_state->base);
	if (ret)
		return ret;

	state2info(plane_state, &info);

	ret = dispc_ovl_check(omap_plane->id,
			omap_crtc_channel(crtc),
			&info, omap_crtc_timings(crtc),
			&x_predecim, &y_predecim);
	if (ret) {
		DBG("%s: dispc_ovl_check failed: %d", omap_plane->name, ret);
		return ret;
	}

	/* TODO add some properties to set max pre-decimation.. but
	 * until then, we'd rather fallback to GPU than decimate:
	 */
	if ((x_predecim > 1) || (y_predecim > 1)) {
		DBG("%s: x_predecim=%d, y_predecim=%d", omap_plane->name,
				x_predecim, y_predecim);
		return -EINVAL;
	}

	return 0;
}

void omap_plane_commit_state(struct drm_plane *plane,
		struct omap_plane_state *plane_state)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_plane_state *old_state = to_omap_plane_state(plane->state);
	struct drm_crtc *crtc;

	DBG("%s", omap_plane->name);

	crtc = plane_state->base.crtc;

	/* TODO we need to handle crtc switch.. we should reject that
	 * at the check() stage if we are still waiting for GO to clear
	 * on the outgoing crtc..
	 */
	if (!crtc)
		crtc = plane->state->crtc;

	drm_plane_commit_state(plane, &plane_state->base);
	kfree(old_state);

	if (crtc)
		omap_crtc_apply(crtc, &omap_plane->apply);
}

static const struct drm_plane_funcs omap_plane_funcs = {
		.destroy = omap_plane_destroy,
		.set_property = omap_plane_set_property,
};

static void omap_plane_error_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_plane *omap_plane =
			container_of(irq, struct omap_plane, error_irq);
	DRM_ERROR("%s: errors: %08x\n", omap_plane->name, irqstatus);
}

static const char *plane_names[] = {
		[OMAP_DSS_GFX] = "gfx",
		[OMAP_DSS_VIDEO1] = "vid1",
		[OMAP_DSS_VIDEO2] = "vid2",
		[OMAP_DSS_VIDEO3] = "vid3",
};

static const uint32_t error_irqs[] = {
		[OMAP_DSS_GFX] = DISPC_IRQ_GFX_FIFO_UNDERFLOW,
		[OMAP_DSS_VIDEO1] = DISPC_IRQ_VID1_FIFO_UNDERFLOW,
		[OMAP_DSS_VIDEO2] = DISPC_IRQ_VID2_FIFO_UNDERFLOW,
		[OMAP_DSS_VIDEO3] = DISPC_IRQ_VID3_FIFO_UNDERFLOW,
};

/* initialize plane */
struct drm_plane *omap_plane_init(struct drm_device *dev,
		int id, bool private_plane)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_plane *plane = NULL;
	struct omap_plane *omap_plane;
	int ret;

	DBG("%s: priv=%d", plane_names[id], private_plane);

	omap_plane = kzalloc(sizeof(*omap_plane), GFP_KERNEL);
	if (!omap_plane) {
		dev_err(dev->dev, "could not allocate plane\n");
		goto fail;
	}

	ret = kfifo_alloc(&omap_plane->unpin_fifo, 16, GFP_KERNEL);
	if (ret) {
		dev_err(dev->dev, "could not allocate unpin FIFO\n");
		goto fail;
	}

	omap_plane->nformats = omap_framebuffer_get_formats(
			omap_plane->formats, ARRAY_SIZE(omap_plane->formats),
			dss_feat_get_supported_color_modes(id));
	omap_plane->id = id;
	omap_plane->name = plane_names[id];

	plane = &omap_plane->base;

	plane->state = kzalloc(sizeof(struct omap_plane_state), GFP_KERNEL);

	if (!plane->state) {
		dev_err(dev->dev, "could not allocate CRTC state\n");
		goto fail;
	}

	omap_plane->apply.pre_apply  = omap_plane_pre_apply;
	omap_plane->apply.post_apply = omap_plane_post_apply;

	omap_plane->error_irq.irqmask = error_irqs[id];
	omap_plane->error_irq.irq = omap_plane_error_irq;
	omap_irq_register(dev, &omap_plane->error_irq);

	drm_plane_init(dev, plane, (1 << priv->num_crtcs) - 1, &omap_plane_funcs,
			omap_plane->formats, omap_plane->nformats, private_plane);

	omap_plane_install_properties(plane, &plane->base);

	/* Set defaults depending on whether we are a CRTC or overlay
	 * layer.
	 */
	if (!private_plane) {
		struct omap_plane_state *plane_state =
				to_omap_plane_state(plane->state);
		plane_state->zorder = id;
		plane_state->enabled = true;
	}

	return plane;

fail:
	if (plane) {
		omap_plane_destroy(plane);
	}
	return NULL;
}
