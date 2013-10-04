/*
 * Copyright (C) 2013 Red Hat
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


#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>

struct drm_atomic_helper_state {
	uint32_t flags;
	struct drm_plane **planes;
	struct drm_plane_state **pstates;
	struct drm_crtc **crtcs;
	struct drm_crtc_state **cstates;
};

/**
 * drm_atomic_helper_begin - start a sequence of atomic updates
 * @dev: DRM device
 * @flags: the modifier flags that userspace has requested
 *
 * Begin a sequence of atomic property sets.  Returns a driver
 * private state object that is passed back into the various
 * object's set_property() fxns, and into the remainder of the
 * atomic funcs.  The state object should accumulate the changes
 * from one o more set_property()'s.  At the end, the state can
 * be checked, and optionally committed.
 *
 * RETURNS
 *   a driver private state object, which is passed back in to
 *   the various other atomic fxns, or error (such as -EBUSY if
 *   there is still a pending async update)
 */
void *drm_atomic_helper_begin(struct drm_device *dev, uint32_t flags)
{
	struct drm_atomic_helper_state *state;
	int nplanes = dev->mode_config.num_plane;
	int ncrtcs  = dev->mode_config.num_crtc;
	int sz;
	void *ptr;

	sz = sizeof(*state);
	sz += (sizeof(state->planes) + sizeof(state->pstates)) * nplanes;
	sz += (sizeof(state->crtcs) + sizeof(state->cstates)) * ncrtcs;

	ptr = kzalloc(sz, GFP_KERNEL);

	state = ptr;
	ptr = &state[1];

	state->flags = flags;

	state->planes = ptr;
	ptr = &state->planes[nplanes];

	state->pstates = ptr;
	ptr = &state->pstates[nplanes];

	state->crtcs = ptr;
	ptr = &state->crtcs[ncrtcs];

	state->cstates = ptr;
	ptr = &state->cstates[ncrtcs];

	return state;
}
EXPORT_SYMBOL(drm_atomic_helper_begin);

/**
 * drm_atomic_helper_set_event - set a pending event on mode object
 * @dev: DRM device
 * @state: the driver private state object
 * @obj: the object to set the event on
 * @event: the event to send back
 *
 * Set pending event for an update on the specified object.  The
 * event is to be sent back to userspace after the update completes.
 */
int drm_atomic_helper_set_event(struct drm_device *dev,
		void *state, struct drm_mode_object *obj,
		struct drm_pending_vblank_event *event)
{
	switch (obj->type) {
	case DRM_MODE_OBJECT_CRTC: {
		struct drm_crtc_state *cstate =
			drm_atomic_get_crtc_state(obj_to_crtc(obj), state);
		cstate->event = event;
		return 0;
	}
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(drm_atomic_helper_set_event);

/**
 * drm_atomic_helper_check - validate state object
 * @dev: DRM device
 * @state: the driver private state object
 *
 * Check the state object to see if the requested state is
 * physically possible.
 *
 * RETURNS
 * Zero for success or -errno
 */
int drm_atomic_helper_check(struct drm_device *dev, void *state)
{
	struct drm_atomic_helper_state *a = state;
	int nplanes = dev->mode_config.num_plane;
	int i, ret = 0;

	for (i = 0; i < nplanes; i++) {
		if (a->planes[i]) {
			ret = drm_atomic_check_plane_state(a->planes[i], a->pstates[i]);
			if (ret)
				break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_check);

/**
 * drm_atomic_helper_commit - commit state
 * @dev: DRM device
 * @state: the driver private state object
 *
 * Commit the state.  This will only be called if atomic_check()
 * succeeds.
 *
 * RETURNS
 * Zero for success or -errno
 */
int drm_atomic_helper_commit(struct drm_device *dev, void *state)
{
	struct drm_atomic_helper_state *a = state;
	int nplanes = dev->mode_config.num_plane;
	int ncrtcs = dev->mode_config.num_crtc;
	int i, ret = 0;

	for (i = 0; i < nplanes; i++) {
		if (a->planes[i]) {
			ret = drm_atomic_commit_plane_state(a->planes[i], a->pstates[i]);
			if (ret)
				break;
		}
	}

	for (i = 0; i < ncrtcs; i++) {
		if (a->crtcs[i]) {
			ret = drm_atomic_commit_crtc_state(a->crtcs[i], a->cstates[i]);
			if (ret)
				break;
		}
	}

	return ret;
}
EXPORT_SYMBOL(drm_atomic_helper_commit);

/**
 * drm_atomic_helper_end - conclude the atomic update
 * @dev: DRM device
 * @state: the driver private state object
 *
 * Release resources associated with the state object.
 */
void drm_atomic_helper_end(struct drm_device *dev, void *state)
{
	struct drm_atomic_helper_state *a = state;
	int nplanes = dev->mode_config.num_plane;
	int ncrtcs = dev->mode_config.num_crtc;
	int i;

	for (i = 0; i < nplanes; i++) {
		if (a->pstates[i]) {
			a->planes[i]->state->state = NULL;
			kfree(a->pstates[i]);
		}
	}

	for (i = 0; i < ncrtcs; i++) {
		if (a->cstates[i]) {
			a->crtcs[i]->state->state = NULL;
			kfree(a->cstates[i]);
		}
	}

	kfree(state);
}
EXPORT_SYMBOL(drm_atomic_helper_end);

int drm_atomic_helper_plane_set_property(struct drm_plane *plane, void *state,
		struct drm_property *property, uint64_t val, void *blob_data)
{
	return drm_plane_set_property(plane,
			drm_atomic_get_plane_state(plane, state),
			property, val, blob_data);
}
EXPORT_SYMBOL(drm_atomic_helper_plane_set_property);

void drm_atomic_helper_init_plane_state(struct drm_plane *plane,
		struct drm_plane_state *pstate, void *state)
{
	/* snapshot current state: */
	*pstate = *plane->state;
	pstate->state = state;
}
EXPORT_SYMBOL(drm_atomic_helper_init_plane_state);

static struct drm_plane_state *
drm_atomic_helper_get_plane_state(struct drm_plane *plane, void *state)
{
	struct drm_atomic_helper_state *a = state;
	struct drm_plane_state *pstate = a->pstates[plane->id];
	if (!pstate) {
		pstate = kzalloc(sizeof(*pstate), GFP_KERNEL);
		drm_atomic_helper_init_plane_state(plane, pstate, state);
		a->planes[plane->id] = plane;
		a->pstates[plane->id] = pstate;
	}
	return pstate;
}

static void
swap_plane_state(struct drm_plane *plane, struct drm_atomic_helper_state *a)
{
	swap(plane->state, a->pstates[plane->id]);
	plane->base.propvals = &plane->state->propvals;
}

static int
drm_atomic_helper_commit_plane_state(struct drm_plane *plane,
		struct drm_plane_state *pstate)
{
	struct drm_device *dev = plane->dev;
	struct drm_framebuffer *old_fb = NULL, *fb = NULL;
	int ret = 0;

	/* probably more fine grain locking would be ok of old crtc
	 * and new crtc were same..
	 */
	drm_modeset_lock_all(dev);

	fb = pstate->fb;

	if (pstate->crtc && pstate->fb) {
		ret = plane->funcs->update_plane(plane, pstate->crtc, pstate->fb,
			pstate->crtc_x, pstate->crtc_y, pstate->crtc_w, pstate->crtc_h,
			pstate->src_x,  pstate->src_y,  pstate->src_w,  pstate->src_h);
		if (!ret) {
			/* on success, update state and fb refcnting: */
			/* NOTE: if we ensure no driver sets plane->state->fb = NULL
			 * on disable, we can move this up a level and not duplicate
			 * nearly the same thing for both update_plane and disable_plane
			 * cases..  I leave it like this for now to be paranoid due to
			 * the slightly different ordering in the two cases in the
			 * original code.
			 */
			old_fb = plane->state->fb;
			swap_plane_state(plane, pstate->state);
			fb = NULL;
		}
	} else {
		old_fb = plane->state->fb;
		plane->funcs->disable_plane(plane);
		swap_plane_state(plane, pstate->state);
	}

	drm_modeset_unlock_all(dev);

	if (fb)
		drm_framebuffer_unreference(fb);
	if (old_fb)
		drm_framebuffer_unreference(old_fb);

	return ret;
}

int drm_atomic_helper_crtc_set_property(struct drm_crtc *crtc, void *state,
		struct drm_property *property, uint64_t val, void *blob_data)
{
	return drm_crtc_set_property(crtc,
			drm_atomic_get_crtc_state(crtc, state),
			property, val, blob_data);
}
EXPORT_SYMBOL(drm_atomic_helper_crtc_set_property);

void drm_atomic_helper_init_crtc_state(struct drm_crtc *crtc,
		struct drm_crtc_state *cstate, void *state)
{
	/* snapshot current state: */
	*cstate = *crtc->state;
	cstate->state = state;
}
EXPORT_SYMBOL(drm_atomic_helper_init_crtc_state);

static struct drm_crtc_state *
drm_atomic_helper_get_crtc_state(struct drm_crtc *crtc, void *state)
{
	struct drm_atomic_helper_state *a = state;
	struct drm_crtc_state *cstate = a->cstates[crtc->id];
	if (!cstate) {
		cstate = kzalloc(sizeof(*cstate), GFP_KERNEL);
		drm_atomic_helper_init_crtc_state(crtc, cstate, state);
		a->crtcs[crtc->id] = crtc;
		a->cstates[crtc->id] = cstate;
	}
	return cstate;
}

static void
swap_crtc_state(struct drm_crtc *crtc, struct drm_atomic_helper_state *a)
{
	swap(crtc->state, a->cstates[crtc->id]);
	crtc->base.propvals = &crtc->state->propvals;
}

static struct drm_connector **get_connector_set(struct drm_device *dev,
		uint32_t *connector_ids, uint32_t num_connector_ids)
{
	struct drm_connector **connector_set = NULL;
	int i;

	connector_set = kmalloc(num_connector_ids *
			sizeof(struct drm_connector *),
			GFP_KERNEL);
	if (!connector_set)
		return NULL;

	for (i = 0; i < num_connector_ids; i++)
		connector_set[i] = drm_connector_find(dev, connector_ids[i]);

	return connector_set;
}

static int set_config(struct drm_crtc *crtc, struct drm_crtc_state *cstate)
{
	struct drm_device *dev = crtc->dev;
	struct drm_framebuffer *fb = cstate->fb;
	struct drm_connector **connector_set = get_connector_set(crtc->dev,
			cstate->connector_ids, cstate->num_connector_ids);
	struct drm_mode_set set = {
			.crtc = crtc,
			.x = cstate->x,
			.y = cstate->y,
			.mode = cstate->mode_valid ? &cstate->mode : NULL,
			.num_connectors = cstate->num_connector_ids,
			.connectors = connector_set,
			.fb = fb,
	};
	int ret;

	drm_modeset_lock_all(dev);

	ret = drm_mode_set_config_internal(&set);
	if (!ret)
		swap_crtc_state(crtc, cstate->state);

	if (fb)
		drm_framebuffer_unreference(fb);

	kfree(connector_set);
	drm_modeset_unlock_all(dev);
	return ret;
}

static int
drm_atomic_helper_commit_crtc_state(struct drm_crtc *crtc,
		struct drm_crtc_state *cstate)
{
	struct drm_framebuffer *old_fb = NULL, *fb = NULL;
	struct drm_atomic_helper_state *a = cstate->state;
	int ret = -EINVAL;

	if (cstate->set_config)
		return set_config(crtc, cstate);

	mutex_lock(&crtc->mutex);
	if (cstate->fb && (crtc->state->fb != cstate->fb)) {
		/* pageflip */

		if (crtc->fb == NULL) {
			/* The framebuffer is currently unbound, presumably
			 * due to a hotplug event, that userspace has not
			 * yet discovered.
			 */
			ret = -EBUSY;
			goto out;
		}

		if (crtc->funcs->page_flip == NULL)
			goto out;

		old_fb = crtc->fb;
		fb = cstate->fb;

		ret = crtc->funcs->page_flip(crtc, fb, cstate->event, a->flags);
		if (ret) {
			/* Keep the old fb, don't unref it. */
			old_fb = NULL;
		} else {
			swap_crtc_state(crtc, cstate->state);
			/* Unref only the old framebuffer. */
			fb = NULL;
		}
	} else if (crtc->state->fb != cstate->fb) {
		/* disable */
		struct drm_mode_set set = {
				.crtc = crtc,
				.fb = NULL,
		};

		old_fb = crtc->state->fb;
		ret = drm_mode_set_config_internal(&set);
		if (!ret) {
			swap_crtc_state(crtc, cstate->state);
		}
	}

out:
	mutex_unlock(&crtc->mutex);

	if (fb)
		drm_framebuffer_unreference(fb);
	if (old_fb)
		drm_framebuffer_unreference(old_fb);

	return ret;
}

const struct drm_atomic_helper_funcs drm_atomic_helper_funcs = {
		.get_plane_state    = drm_atomic_helper_get_plane_state,
		.check_plane_state  = drm_plane_check_state,
		.commit_plane_state = drm_atomic_helper_commit_plane_state,

		.get_crtc_state     = drm_atomic_helper_get_crtc_state,
		.check_crtc_state   = drm_crtc_check_state,
		.commit_crtc_state  = drm_atomic_helper_commit_crtc_state,
};
EXPORT_SYMBOL(drm_atomic_helper_funcs);
