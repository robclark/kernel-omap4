/*
 * drivers/staging/omapdrm/omap_atomic.c
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

#include "omap_drv.h"
#include "omap_atomic.h"

struct omap_atomic_state {
	struct drm_device *dev;

	/* for page-flips, this is the CRTC involved: */
	struct drm_crtc *crtc;
	int pipe;

	int num_dirty_planes, num_dirty_crtcs;
	struct omap_plane_state *plane_state[8];
	struct omap_crtc_state  *crtc_state[8];

	int num_pending_fbs;
	atomic_t num_ready_fbs;
	struct drm_framebuffer  *pending_fbs[8];

	/* for handling page flips without caring about what
	 * the callback is called from.  Possibly we should just
	 * make omap_gem always call the cb from the worker so
	 * we don't have to care about this..
	 */
	struct work_struct commit_work;
};

static void commit_worker(struct work_struct *work);

static void set_atomic(struct omap_atomic_state *omap_state, bool atomic)
{
	struct omap_drm_private *priv = omap_state->dev->dev_private;
	if (omap_state->crtc) {
		int pipe = omap_state->pipe;
		priv->crtc_atomic[pipe] += (atomic ? 1 : -1);
	} else {
		priv->global_atomic = atomic;
	}
}

void *omap_atomic_begin(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_atomic_state *omap_state;
	int pipe = 0;

	if (crtc) {
		pipe = crtc2pipe(dev, crtc);
		if (priv->event[pipe]) {
			dev_err(dev->dev, "pending page-flip!\n");
			return ERR_PTR(-EBUSY);
		}
		WARN_ON(priv->crtc_atomic[pipe]);
	} else {
		WARN_ON(priv->global_atomic);
	}

	omap_state = kzalloc(sizeof(*omap_state), GFP_KERNEL);
	if (!omap_state) {
		dev_err(dev->dev, "failed to allocate state\n");
		return ERR_PTR(-ENOMEM);
	}

	omap_state->dev = dev;
	omap_state->crtc = crtc;
	omap_state->pipe = pipe;

	INIT_WORK(&omap_state->commit_work, commit_worker);

	set_atomic(omap_state, true);

	DBG("state=%p, crtc=%p", omap_state, crtc);

	return omap_state;
}

static void release_state(struct omap_atomic_state *omap_state)
{
	int i;

	DBG("state=%p", omap_state);

	for (i = 0; i < omap_state->num_pending_fbs; i++)
		drm_framebuffer_unreference(omap_state->pending_fbs[i]);

	/*
	 * omap_plane_commit()/omap_crtc_commit() have taken ownership
	 * of their respective state objects, so don't need to kfree()
	 * 'em here
	 */

	kfree(omap_state);
}

int omap_atomic_check(struct drm_device *dev, void *state)
{
	struct omap_atomic_state *omap_state = state;
	struct omap_drm_private *priv = dev->dev_private;
	int i, ret = 0;

	for (i = 0; (i < ARRAY_SIZE(omap_state->plane_state)) && !ret; i++)
		if (omap_state->plane_state[i])
			ret = omap_plane_check_state(priv->planes[i],
					omap_state->plane_state[i]);

	for (i = 0; (i < ARRAY_SIZE(omap_state->crtc_state)) && !ret; i++)
		if (omap_state->crtc_state[i])
			ret = omap_crtc_check_state(priv->crtcs[i],
					omap_state->crtc_state[i]);

	DBG("state=%p, ret=%d", omap_state, ret);

	if (ret) {
		set_atomic(omap_state, false);
		release_state(omap_state);
	}

	return ret;
}

static void commit_state(struct omap_atomic_state *omap_state)
{
	struct drm_device *dev = omap_state->dev;
	struct omap_drm_private *priv = dev->dev_private;
	int i;

	DBG("state=%p", omap_state);

	for (i = 0; i < ARRAY_SIZE(omap_state->plane_state); i++) {
		struct omap_plane_state *plane_state =
				omap_state->plane_state[i];
		if (plane_state)
			omap_plane_commit_state(priv->planes[i], plane_state);
	}

	set_atomic(omap_state, false);

	for (i = 0; i < ARRAY_SIZE(omap_state->crtc_state); i++) {
		struct omap_crtc_state *crtc_state =
				omap_state->crtc_state[i];
		if (crtc_state)
			omap_crtc_commit_state(priv->crtcs[i], crtc_state);
	}

	release_state(omap_state);
}

static void commit_worker(struct work_struct *work)
{
	struct omap_atomic_state *omap_state =
			container_of(work, struct omap_atomic_state, commit_work);
	struct drm_device *dev = omap_state->dev;

	mutex_lock(&dev->mode_config.mutex);
	DBG("state=%p", omap_state);
	commit_state(omap_state);
	mutex_unlock(&dev->mode_config.mutex);
}

static void commit_cb(void *state)
{
	struct omap_atomic_state *omap_state = state;
	struct omap_drm_private *priv = omap_state->dev->dev_private;
	int num_ready_fbs = atomic_inc_return(&omap_state->num_ready_fbs);

	if (num_ready_fbs == omap_state->num_pending_fbs)
		queue_work(priv->wq, &omap_state->commit_work);
}

static void commit_async(struct drm_device *dev, void *state,
		struct drm_pending_vblank_event *event)
{
	struct omap_atomic_state *omap_state = state;
	struct omap_drm_private *priv = omap_state->dev->dev_private;
	int i;

	if (event) {
		int pipe = omap_state->pipe;
		WARN_ON(priv->event[pipe]);
		priv->event[pipe] = event;
	}

	if (!omap_state->num_pending_fbs) {
		commit_state(omap_state);
		return;
	}

	for (i = 0; i < omap_state->num_pending_fbs; i++) {
		struct drm_gem_object *bo;
		bo = omap_framebuffer_bo(omap_state->pending_fbs[i], 0);
		omap_gem_op_async(bo, OMAP_GEM_READ, commit_cb, omap_state);
	}
}

int omap_atomic_commit(struct drm_device *dev, void *state,
		struct drm_pending_vblank_event *event)
{
	struct omap_atomic_state *omap_state = state;

	DBG("state=%p, event=%p", omap_state, event);

	if (omap_state->crtc) {
		/* async page-flip */
		commit_async(dev, state, event);
	} else {
		/* sync mode-set, etc */
		WARN_ON(event);  /* this should not happen */
		commit_state(omap_state);
	}

	return 0;
}

void omap_atomic_end(struct drm_device *dev, void *state)
{
	/*
	 * State is freed either if atomic_check() fails or
	 * when async pageflip completes, so we don't need
	 * to do anything here.
	 */
}

struct omap_plane_state *omap_atomic_plane_state(void *state, int id)
{
	struct omap_atomic_state *omap_state = state;
	struct omap_drm_private *priv = omap_state->dev->dev_private;
	struct omap_plane_state *plane_state = omap_state->plane_state[id];
	int i;

	if (!plane_state) {
		struct drm_plane *plane = priv->planes[id];

		plane_state = kzalloc(sizeof(*plane_state), GFP_KERNEL);

		/* snapshot current state: */
		*plane_state = *to_omap_plane_state(plane->state);

		omap_state->plane_state[id] = plane_state;
	}

	/* updating a plane implicitly dirties the crtc: */
	for (i = 0; i < priv->num_crtcs; i++) {
		if (priv->crtcs[i] == plane_state->base.crtc) {
			omap_atomic_crtc_state(state, i);
			break;
		}
	}

	return plane_state;
}

struct omap_crtc_state *omap_atomic_crtc_state(void *state, int id)
{
	struct omap_atomic_state *omap_state = state;
	struct omap_drm_private *priv = omap_state->dev->dev_private;
	struct omap_crtc_state *crtc_state = omap_state->crtc_state[id];

	if (!crtc_state) {
		struct drm_crtc *crtc = priv->crtcs[id];

		crtc_state = kzalloc(sizeof(*crtc_state), GFP_KERNEL);

		/* snapshot current state: */
		*crtc_state = *to_omap_crtc_state(crtc->state);

		omap_state->crtc_state[id] = crtc_state;
	}

	return crtc_state;
}

/* when fb is changed, that gets recorded in the state, so that pageflips
 * can defer until all fb's are ready
 */
void omap_atomic_add_fb(void *state, struct drm_framebuffer *fb)
{
	struct omap_atomic_state *omap_state = state;
	drm_framebuffer_reference(fb);
	omap_state->pending_fbs[omap_state->num_pending_fbs++] = fb;
}

/* possibly this could be in drm core? */
static void send_page_flip_event(struct drm_device *dev, int crtc,
		struct drm_pending_vblank_event *event)
{
	unsigned long flags;
	struct timeval now;

	DBG("%p", event);

	spin_lock_irqsave(&dev->event_lock, flags);
	event->event.sequence = drm_vblank_count_and_time(dev, crtc, &now);
	event->event.tv_sec = now.tv_sec;
	event->event.tv_usec = now.tv_usec;
	list_add_tail(&event->base.link,
			&event->base.file_priv->event_list);
	wake_up_interruptible(&event->base.file_priv->event_wait);
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

/* called when plane is updated.. so we can keep track of when to send
 * page-flip events
 */
void omap_atomic_plane_update(struct drm_device *dev, int id)
{
	struct omap_drm_private *priv = dev->dev_private;
	int pipe = crtc2pipe(dev, priv->planes[id]->state->crtc);

	DBG("id=%d", id);

	if (priv->event[pipe]) {
		/* wakeup userspace */
		send_page_flip_event(dev, pipe, priv->event[pipe]);
		priv->event[pipe] = NULL;
	}
}
