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

#ifndef DRM_ATOMIC_HELPER_H_
#define DRM_ATOMIC_HELPER_H_

/**
 * DOC: atomic state helpers
 *
 * Base helper atomic state and functions.  Drivers are free to either
 * use these as-is, extend them, or completely replace them, in order
 * to implement the atomic KMS API.
 *
 * A naive driver, with no special constraints or hw support for atomic
 * updates may simply add the following to their driver struct:
 *
 *     .atomic_begin     = drm_atomic_helper_begin,
 *     .atomic_set_event = drm_atomic_helper_set_event,
 *     .atomic_check     = drm_atomic_helper_check,
 *     .atomic_commit    = drm_atomic_helper_commit,
 *     .atomic_end       = drm_atomic_helper_end,
 *     .atomic_helpers   = &drm_atomic_helper_funcs,
 *
 * In addition, if you're plane/crtc doesn't already have it's own custom
 * properties, then add to your plane/crtc_funcs:
 *
 *     .set_property     = drm_atomic_helper_{plane,crtc}_set_property,
 *
 * Unlike the crtc helpers, it is intended that the atomic helpers can be
 * used piecemeal by the drivers, either using all or overriding parts as
 * needed.
 *
 * A driver which can have (for example) conflicting modes across multiple
 * crtcs (for example, bandwidth limitations or clock/pll configuration
 * restrictions), can simply wrap drm_atomic_helper_check() with their own
 * driver specific .atomic_check() function.
 *
 * A driver which can support true atomic updates can wrap
 * drm_atomic_helper_commit().
 *
 * A driver with custom properties should override the appropriate get_state(),
 * check_state(), and commit_state() functions in .atomic_helpers if it uses
 * the drm-atomic-helpers.  Otherwise it is free to use &drm_atomic_helper_funcs
 * as-is.
 *
 * NOTE: currently all drivers using the atomic helpers must use
 * drm_atomic_helper_begin.  But if there is a good use-case for a driver
 * overriding .atomic_begin we can loosen this restriction by making
 * 'struct drm_atomic_helper_state' public and only requiring that the
 * driver wrap/subclass drm_atomic_helper_state.
 */

/**
 * struct drm_atomic_helper_funcs - helper funcs used by the atomic helpers
 */
struct drm_atomic_helper_funcs {
	struct drm_plane_state *(*get_plane_state)(struct drm_plane *plane, void *state);
	int (*check_plane_state)(struct drm_plane *plane, struct drm_plane_state *pstate);
	int (*commit_plane_state)(struct drm_plane *plane, struct drm_plane_state *pstate);
};

const extern struct drm_atomic_helper_funcs drm_atomic_helper_funcs;

void *drm_atomic_helper_begin(struct drm_device *dev, uint32_t flags);
int drm_atomic_helper_set_event(struct drm_device *dev,
		void *state, struct drm_mode_object *obj,
		struct drm_pending_vblank_event *event);
int drm_atomic_helper_check(struct drm_device *dev, void *state);
int drm_atomic_helper_commit(struct drm_device *dev, void *state);
void drm_atomic_helper_end(struct drm_device *dev, void *state);

int drm_atomic_helper_plane_set_property(struct drm_plane *plane, void *state,
		struct drm_property *property, uint64_t val, void *blob_data);
void drm_atomic_helper_init_plane_state(struct drm_plane *plane,
		struct drm_plane_state *pstate, void *state);

static inline struct drm_plane_state *
drm_atomic_get_plane_state(struct drm_plane *plane, void *state)
{
	const struct drm_atomic_helper_funcs *funcs =
			plane->dev->driver->atomic_helpers;
	return funcs->get_plane_state(plane, state);
}

static inline int
drm_atomic_check_plane_state(struct drm_plane *plane,
		struct drm_plane_state *pstate)
{
	const struct drm_atomic_helper_funcs *funcs =
			plane->dev->driver->atomic_helpers;
	return funcs->check_plane_state(plane, pstate);
}

static inline int
drm_atomic_commit_plane_state(struct drm_plane *plane,
		struct drm_plane_state *pstate)
{
	const struct drm_atomic_helper_funcs *funcs =
			plane->dev->driver->atomic_helpers;
	return funcs->commit_plane_state(plane, pstate);
}

#endif /* DRM_ATOMIC_HELPER_H_ */
