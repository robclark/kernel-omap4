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
	int sz;
	void *ptr;

	sz = sizeof(*state);
	sz += (sizeof(state->planes) + sizeof(state->pstates)) * nplanes;

	ptr = kzalloc(sz, GFP_KERNEL);

	state = ptr;
	ptr = &state[1];

	state->flags = flags;

	state->planes = ptr;
	ptr = &state->planes[nplanes];

	state->pstates = ptr;
	ptr = &state->pstates[nplanes];

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
	return -EINVAL;  /* for now */
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
	int i, ret = 0;

	for (i = 0; i < nplanes; i++) {
		if (a->planes[i]) {
			ret = drm_atomic_commit_plane_state(a->planes[i], a->pstates[i]);
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
	int i;

	for (i = 0; i < nplanes; i++) {
		if (a->pstates[i]) {
			a->planes[i]->state->state = NULL;
			kfree(a->pstates[i]);
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

const struct drm_atomic_helper_funcs drm_atomic_helper_funcs = {
		.get_plane_state    = drm_atomic_helper_get_plane_state,
		.check_plane_state  = drm_plane_check_state,
		.commit_plane_state = drm_atomic_helper_commit_plane_state,
};
EXPORT_SYMBOL(drm_atomic_helper_funcs);
