/*
 * Copyright (C) 2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and iated documentation files (the "Software"),
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

#ifndef DRM_FLIP_H
#define DRM_FLIP_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

struct drm_flip;
struct drm_flip_helper;
struct drm_flip_driver;

/* Driver callbacks for drm_flip_driver */
struct drm_flip_driver_funcs {
	/*
	 * Optional callback, called after drm_flip_driver_schedule_flips()
	 * has called drm_flip_helper::flip() for all the provided flips.
	 * Can be used to:
	 * - commit the flips atomically to the hardware, if the
	 *   hardware provides some mechanism to do that.
	 * - flush posted writes to make sure all the flips have reached
	 *   the hardware
	 * Called with drm_flip_driver::lock held.
	 */
	void (*flush)(struct drm_flip_driver *driver);
};

/*
 * The driver needs one drm_flip_driver to
 * coordinates the drm_flip mechanism.
 */
struct drm_flip_driver {
	/* protects drm_flip_driver, drm_flip_helper, and drm_flip internals. */
	spinlock_t lock;

	/* list of drm_flips waiting to be finished, protected by 'lock' */
	struct list_head finish_list;

	/* list of drm_flips waiting to be cleaned up, protected by 'lock' */
	struct list_head cleanup_list;

	/* work used to finish the drm_flips */
	struct work_struct finish_work;

	/* work used to clean up the drm_flips */
	struct work_struct cleanup_work;

	/* driver provided callback functions */
	const struct drm_flip_driver_funcs *funcs;

	/* work queue for finish_work and cleanup_work */
	struct workqueue_struct *wq;
};

/* Driver callbacks for drm_flip_helper */
struct drm_flip_helper_funcs {
	/*
	 * Optional function to perform heavy but non-timing
	 * critial preparations for the flip.
	 * Called from drm_flip_driver_prepare_flips() with
	 * no extra locks being held.
	 */
	void (*prepare)(struct drm_flip *flip);
	/*
	 * Instruct the hardware to flip on the next vblank.
	 * Must return true, iff pending_flip exists, and has
	 * actually flipped (ie. now being scanned out).
	 * Otherwise must return false.
	 * Called with drm_flip_driver::lock held.
	 */
	bool (*flip)(struct drm_flip *flip,
		     struct drm_flip *pending_flip);
	/*
	 * Called from drm_flip_helper_vblank() if
	 * pending_flip exists. Must return true, iff
	 * pending_flip has actually flipped (ie. now
	 * being scanned out). Otherwise must return false.
	 * Called with drm_flip_driver::lock held.
	 */
	bool (*vblank)(struct drm_flip *pending_flip);

	/*
	 * The flip has just occured, or it got overwritten
	 * by a more recent flip. If the flip occured, it is
	 * now being scanned out, otherwise it is scheduled
	 * for cleanup.
	 * Can be called from drm_flip_driver_schedule_flips(),
	 * drm_flip_driver_complete_flips(), or from
	 * drm_flip_helper_vblank().
	 * Called with drm_flip_driver::lock held.
	 */
	void (*complete)(struct drm_flip *flip);

	/*
	 * Perform finishing steps on the flip. Called from a workqueue
	 * soon after the flip has completed. The flip's buffer may be
	 * actively scanned out.
	 * Called with no locks being held.
	 */
	void (*finish)(struct drm_flip *flip);

	/*
	 * Perform final cleanup on the flip. Called from a workqueue
	 * after the flip's buffer is no longer being scanned out.
	 * Called with no locks being held.
	 */
	void (*cleanup)(struct drm_flip *flip);

};

/*
 * The driver needs one drm_flip_helper for each scanout engine it
 * wants to operate through the drm_flip mechanism.
 */
struct drm_flip_helper {
	/* drm_flip from the previous drm_flip_schedule() call */
	struct drm_flip *pending_flip;
	/* drm_flip whose buffer is being scanned out */
	struct drm_flip *scanout_flip;
	/* associated drm_flip_driver */
	struct drm_flip_driver *driver;
	/* driver provided callback functions */
	const struct drm_flip_helper_funcs *funcs;
};

/*
 * This structure represents a single page flip operation.
 */
struct drm_flip {
	/* associated drm_flip_helper */
	struct drm_flip_helper *helper;
	/* has this flip occured? */
	bool flipped;
	/* has the finish work been executed for this flip? */
	bool finished;
	/* used to keep this flip on various lists */
	struct list_head list;
};

/*
 * Initialize the flip driver.
 */
void drm_flip_driver_init(struct drm_flip_driver *driver,
			  const struct drm_flip_driver_funcs *funcs);

/*
 * Finalize the flip driver. This will block until all the
 * pending finish and cleanup work has been completed.
 */
void drm_flip_driver_fini(struct drm_flip_driver *driver);

/*
 * Initialize flip helper.
 */
void drm_flip_helper_init(struct drm_flip_helper *helper,
			  struct drm_flip_driver *driver,
			  const struct drm_flip_helper_funcs *funcs);

/*
 * Clear flip helper state. This will forcefully complete the
 * helper's pending flip (if any).
 */
void drm_flip_helper_clear(struct drm_flip_helper *helper);

/*
 * Finalize the flip helper. This will forcefully complete the
 * helper's pending flip (if any), and wait for the finish and
 * cleanup works to finish.
 */
void drm_flip_helper_fini(struct drm_flip_helper *helper);

/*
 * Call this from the driver's vblank handler for the scanout engine
 * associated with this helper.
 */
void drm_flip_helper_vblank(struct drm_flip_helper *helper);

/*
 * This will call drm_flip_helper::prepare() (if provided) for all the
 * drm_flips on the list. The purpose is to perform any non-timing critical
 * preparation steps for the flips before taking locks or disabling interrupts.
 */
void drm_flip_driver_prepare_flips(struct drm_flip_driver *driver,
				   struct list_head *flips);

/*
 * Schedule the flips on the list to occur on the next vblank.
 *
 * This will call drm_flip_helper::flip() for all the drm_flips on the list.
 * It will then call drm_flip_driver::flush(), after which it will complete
 * any pending_flip that got overridden by the new flips.
 *
 * Unless the hardware provides some mechanism to synchronize the flips, the
 * time spent until drm_flip_driver::flush() is timing critical and the driver
 * must somehow make sure it can complete the operation in a seemingly atomic
 * fashion.
 */
void drm_flip_driver_schedule_flips(struct drm_flip_driver *driver,
				    struct list_head *flips);

/*
 * This will complete any pending_flip and also all the flips
 * on the provided list (in that order).
 *
 * Call this instead of drm_flip_driver_schedule_flips()
 * eg. if the hardware powered down, and you just want to keep
 * the drm_flip mechanim's state consistent w/o waking up the
 * hardware.
 */
void drm_flip_driver_complete_flips(struct drm_flip_driver *driver,
				    struct list_head *flips);

/*
 * Initialize the flip structure members.
 */
void drm_flip_init(struct drm_flip *flip,
		   struct drm_flip_helper *helper);

#endif
