/*
 * drivers/staging/omapdrm/omap_atomic.h
 *
 * Copyright (C) 2012 Texas Instruments
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

#ifndef __OMAP_ATOMIC_H__
#define __OMAP_ATOMIC_H__

#include "drm_mode.h"
#include "drm_crtc.h"

struct omap_plane_state {
	struct drm_plane_state base;
	uint8_t rotation;
	uint8_t zorder;
	uint8_t enabled;
};
#define to_omap_plane_state(x) container_of(x, struct omap_plane_state, base)

struct omap_crtc_state {
	struct drm_crtc_state base;
};
#define to_omap_crtc_state(x) container_of(x, struct omap_crtc_state, base)

void *omap_atomic_begin(struct drm_device *dev, struct drm_crtc *crtc);
int omap_atomic_check(struct drm_device *dev, void *state);
int omap_atomic_commit(struct drm_device *dev, void *state,
		struct drm_pending_vblank_event *event);
void omap_atomic_end(struct drm_device *dev, void *state);

struct omap_plane_state *omap_atomic_plane_state(void *state, int id);
struct omap_crtc_state *omap_atomic_crtc_state(void *state, int id);

void omap_atomic_add_fb(void *state, struct drm_framebuffer *fb);

void omap_atomic_plane_update(struct drm_device *dev, int id);

#endif /* __OMAP_ATOMIC_H__ */
