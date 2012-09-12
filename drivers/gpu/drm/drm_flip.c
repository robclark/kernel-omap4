/*
 * Copyright (C) 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 * Ville Syrjälä <ville.syrjala@linux.intel.com>
 */

#include <drm/drm_flip.h>

static void drm_flip_driver_cleanup(struct work_struct *work)
{
	struct drm_flip *flip, *next;
	struct drm_flip_driver *driver =
		container_of(work, struct drm_flip_driver, cleanup_work);
	LIST_HEAD(list);

	spin_lock_irq(&driver->lock);

	list_cut_position(&list,
			  &driver->cleanup_list,
			  driver->cleanup_list.prev);

	spin_unlock_irq(&driver->lock);

	if (list_empty(&list))
		return;

	list_for_each_entry_safe(flip, next, &list, list) {
		struct drm_flip_helper *helper = flip->helper;

		WARN_ON(!flip->finished);

		helper->funcs->cleanup(flip);
	}
}

static void drm_flip_driver_finish(struct work_struct *work)
{
	struct drm_flip *flip, *next;
	struct drm_flip_driver *driver =
		container_of(work, struct drm_flip_driver, finish_work);
	LIST_HEAD(list);
	bool need_cleanup = false;

	spin_lock_irq(&driver->lock);

	list_cut_position(&list,
			  &driver->finish_list,
			  driver->finish_list.prev);

	spin_unlock_irq(&driver->lock);

	if (list_empty(&list))
		return;

	list_for_each_entry_safe(flip, next, &list, list) {
		struct drm_flip_helper *helper = flip->helper;

		helper->funcs->finish(flip);

		spin_lock_irq(&driver->lock);

		flip->finished = true;

		/*
		 * It's possible that drm_flip_set_scanout() was called after we
		 * pulled this flip from finish_list, in which case the flip
		 * could be in need of cleanup, but not on cleanup_list.
		 */
		if (flip == helper->scanout_flip) {
			list_del_init(&flip->list);
		} else {
			need_cleanup = true;
			list_move_tail(&flip->list, &driver->cleanup_list);
		}

		spin_unlock_irq(&driver->lock);
	}

	if (need_cleanup)
		queue_work(driver->wq, &driver->cleanup_work);
}

static bool drm_flip_set_scanout(struct drm_flip_helper *helper,
				 struct drm_flip *flip)
{
	struct drm_flip_driver *driver = helper->driver;
	struct drm_flip *old = helper->scanout_flip;

	helper->scanout_flip = flip;

	if (old && old->finished)
		list_move_tail(&old->list, &driver->cleanup_list);

	return old != NULL;
}

static bool drm_flip_complete(struct drm_flip *flip)
{
	struct drm_flip_helper *helper = flip->helper;
	struct drm_flip_driver *driver = helper->driver;
	bool need_cleanup = false;

	helper->funcs->complete(flip);

	if (flip->flipped) {
		if (drm_flip_set_scanout(helper, flip))
			need_cleanup = true;
	}

	list_add_tail(&flip->list, &driver->finish_list);

	return need_cleanup;
}

void drm_flip_helper_init(struct drm_flip_helper *helper,
			  struct drm_flip_driver *driver,
			  const struct drm_flip_helper_funcs *funcs)
{
	helper->pending_flip = NULL;
	helper->scanout_flip = NULL;
	helper->driver = driver;
	helper->funcs = funcs;
}

void drm_flip_helper_clear(struct drm_flip_helper *helper)
{
	unsigned long flags;
	struct drm_flip_driver *driver = helper->driver;
	struct drm_flip *pending_flip;
	bool need_finish = false;
	bool need_cleanup = false;

	spin_lock_irqsave(&driver->lock, flags);

	pending_flip = helper->pending_flip;

	if (pending_flip) {
		BUG_ON(pending_flip->helper != helper);

		need_finish = true;

		if (drm_flip_complete(pending_flip))
			need_cleanup = true;

		helper->pending_flip = NULL;
	}

	if (drm_flip_set_scanout(helper, NULL))
		need_cleanup = true;

	spin_unlock_irqrestore(&driver->lock, flags);

	if (need_finish)
		queue_work(driver->wq, &driver->finish_work);

	if (need_cleanup)
		queue_work(driver->wq, &driver->cleanup_work);
}

void drm_flip_helper_fini(struct drm_flip_helper *helper)
{
	struct drm_flip_driver *driver = helper->driver;

	drm_flip_helper_clear(helper);

	flush_work_sync(&driver->finish_work);
	flush_work_sync(&driver->cleanup_work);
}

void drm_flip_helper_vblank(struct drm_flip_helper *helper)
{
	struct drm_flip_driver *driver = helper->driver;
	struct drm_flip *pending_flip;
	unsigned long flags;
	bool need_finish = false;
	bool need_cleanup = false;

	spin_lock_irqsave(&driver->lock, flags);

	pending_flip = helper->pending_flip;

	if (pending_flip) {
		BUG_ON(pending_flip->helper != helper);

		if (helper->funcs->vblank(pending_flip))
			pending_flip->flipped = true;

		if (pending_flip->flipped) {
			need_finish = true;

			if (drm_flip_complete(pending_flip))
				need_cleanup = true;

			helper->pending_flip = NULL;
		}
	}

	spin_unlock_irqrestore(&driver->lock, flags);

	if (need_finish)
		queue_work(driver->wq, &driver->finish_work);

	if (need_cleanup)
		queue_work(driver->wq, &driver->cleanup_work);
}

void drm_flip_driver_init(struct drm_flip_driver *driver,
			  const struct drm_flip_driver_funcs *funcs)
{
	spin_lock_init(&driver->lock);

	INIT_LIST_HEAD(&driver->finish_list);
	INIT_LIST_HEAD(&driver->cleanup_list);

	INIT_WORK(&driver->finish_work, drm_flip_driver_finish);
	INIT_WORK(&driver->cleanup_work, drm_flip_driver_cleanup);

	driver->funcs = funcs;

	driver->wq = create_singlethread_workqueue("drm_flip");
}

void drm_flip_driver_fini(struct drm_flip_driver *driver)
{
	destroy_workqueue(driver->wq);

	/* All the scheduled flips should be cleaned up by now. */
	WARN_ON(!list_empty(&driver->finish_list));
	WARN_ON(!list_empty(&driver->cleanup_list));
}

void drm_flip_driver_schedule_flips(struct drm_flip_driver *driver,
				    struct list_head *flips)
{
	unsigned long flags;
	struct drm_flip *flip, *next;
	bool need_finish = false;
	bool need_cleanup = false;

	spin_lock_irqsave(&driver->lock, flags);

	list_for_each_entry(flip, flips, list) {
		struct drm_flip_helper *helper = flip->helper;
		struct drm_flip *pending_flip = helper->pending_flip;

		if (helper->funcs->flip(flip, pending_flip))
			pending_flip->flipped = true;
	}

	if (driver->funcs->flush)
		driver->funcs->flush(driver);

	/* Complete all flips that got overridden */
	list_for_each_entry_safe(flip, next, flips, list) {
		struct drm_flip_helper *helper = flip->helper;
		struct drm_flip *pending_flip = helper->pending_flip;

		BUG_ON(helper->driver != driver);

		if (pending_flip) {
			BUG_ON(pending_flip->helper != helper);

			need_finish = true;

			if (drm_flip_complete(pending_flip))
				need_cleanup = true;
		}

		list_del_init(&flip->list);
		helper->pending_flip = flip;
	}

	spin_unlock_irqrestore(&driver->lock, flags);

	if (need_finish)
		queue_work(driver->wq, &driver->finish_work);

	if (need_cleanup)
		queue_work(driver->wq, &driver->cleanup_work);
}

void drm_flip_driver_prepare_flips(struct drm_flip_driver *driver,
				   struct list_head *flips)
{
	struct drm_flip *flip;

	list_for_each_entry(flip, flips, list) {
		struct drm_flip_helper *helper = flip->helper;

		if (helper->funcs->prepare)
			helper->funcs->prepare(flip);
	}
}

void drm_flip_driver_complete_flips(struct drm_flip_driver *driver,
				    struct list_head *flips)
{
	unsigned long flags;
	struct drm_flip *flip, *next;
	bool need_finish = false;
	bool need_cleanup = false;

	spin_lock_irqsave(&driver->lock, flags);

	/* first complete all pending flips */
	list_for_each_entry(flip, flips, list) {
		struct drm_flip_helper *helper = flip->helper;
		struct drm_flip *pending_flip = helper->pending_flip;

		BUG_ON(helper->driver != driver);

		if (pending_flip) {
			BUG_ON(pending_flip->helper != helper);

			need_finish = true;

			if (drm_flip_complete(pending_flip))
				need_cleanup = true;

			helper->pending_flip = NULL;
		}
	}

	/* then complete all new flips as well */
	list_for_each_entry_safe(flip, next, flips, list) {
		list_del_init(&flip->list);

		/*
		 * This is the flip that gets scanned out
		 * next time the hardware is fired up.
		 */
		flip->flipped = true;

		need_finish = true;

		if (drm_flip_complete(flip))
			need_cleanup = true;
	}

	spin_unlock_irqrestore(&driver->lock, flags);

	if (need_finish)
		queue_work(driver->wq, &driver->finish_work);

	if (need_cleanup)
		queue_work(driver->wq, &driver->cleanup_work);
}

void drm_flip_init(struct drm_flip *flip,
		   struct drm_flip_helper *helper)
{
	flip->helper = helper;
	flip->flipped = false;
	flip->finished = false;
	INIT_LIST_HEAD(&flip->list);
}
