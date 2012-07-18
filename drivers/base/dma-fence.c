/*
 * Fence mechanism for dma-buf to allow for asynchronous dma access
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

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>


/* TODO probably drop this fxn */
static void signal_fence(struct dma_fence *fence)
{
	wake_up_all_locked(&fence->event_queue);
}

/**
 * dma_fence_signal - Signal a fence.
 *
 * @fence:  The fence to signal
 *
 * All registered callbacks will be called directly (synchronously) and
 * all blocked waters will be awoken.
 *
 * TODO: any value in adding a dma_fence_cancel(), for example to recov
 * from hung gpu?  It would behave like dma_fence_signal() but return
 * an error to waiters and cb's to let them know that the condition they
 * are waiting for will never happen.
 */
int dma_fence_signal(struct dma_fence *fence)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (WARN_ON(!fence))
		return -EINVAL;

	spin_lock_irqsave(&fence->event_queue.lock, flags);
	if (!fence->signaled) {
		fence->signaled = true;
		signal_fence(fence);
		ret = 0;
	}
	spin_unlock_irqrestore(&fence->event_queue.lock, flags);

	dma_fence_put(fence);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_fence_signal);

static void release_fence(struct kref *kref)
{
	struct dma_fence *fence =
			container_of(kref, struct dma_fence, refcount);

	WARN_ON(waitqueue_active(&fence->event_queue));

	kfree(fence);
}

/**
 * dma_fence_put - Release a reference to the fence.
 */
void dma_fence_put(struct dma_fence *fence)
{
	WARN_ON(!fence);
	kref_put(&fence->refcount, release_fence);
}
EXPORT_SYMBOL_GPL(dma_fence_put);

/**
 * dma_fence_get - Take a reference to the fence.
 *
 * In most cases this is used only internally by dma-fence.
 */
void dma_fence_get(struct dma_fence *fence)
{
	WARN_ON(!fence);
	kref_get(&fence->refcount);
}
EXPORT_SYMBOL_GPL(dma_fence_get);

static int check_signaling(struct dma_fence *fence)
{
	bool enable_signaling = false;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&fence->event_queue.lock, flags);
	if (!fence->needs_sw_signal && !fence->signaled)
		enable_signaling = fence->needs_sw_signal = true;
	spin_unlock_irqrestore(&fence->event_queue.lock, flags);

	if (enable_signaling)
		ret = fence->ops->enable_signaling(fence);

	return ret;
}

/**
 * dma_fence_add_callback - Add a callback to be called when the fence
 * is signaled.
 *
 * @fence: The fence to wait on
 * @cb: The callback to register
 *
 * Any number of callbacks can be registered to a fence, but a callback
 * can only be registered to once fence at a time.
 *
 * Note that the callback can be called from an atomic context.  If
 * fence is already signaled, this function will return -ENOENT (and
 * *not* call the callback)
 */
int dma_fence_add_callback(struct dma_fence *fence,
		struct dma_fence_cb *cb)
{
	unsigned long flags;
	int ret;

	if (WARN_ON(!fence || !cb))
		return -EINVAL;

	ret = check_signaling(fence);

	spin_lock_irqsave(&fence->event_queue.lock, flags);
	if (ret == -ENOENT) {
		/* if state changed while we dropped the lock, dispatch now */
		signal_fence(fence);
	} else if (!fence->signaled && !ret) {
		dma_fence_get(fence);
		cb->fence = fence;
		__add_wait_queue(&fence->event_queue, &cb->base);
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&fence->event_queue.lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_fence_add_callback);

/**
 * dma_fence_cancel_callback - Remove a previously registered callback.
 *
 * @cb: The callback to unregister
 *
 * The callback will not be called after this function returns, but could
 * be called before this function returns.
 */
int dma_fence_cancel_callback(struct dma_fence_cb *cb)
{
	struct dma_fence *fence;
	unsigned long flags;
	int ret = -EINVAL;

	if (WARN_ON(!cb))
		return -EINVAL;

	fence = cb->fence;

	spin_lock_irqsave(&fence->event_queue.lock, flags);
	if (fence) {
		__remove_wait_queue(&fence->event_queue, &cb->base);
		cb->fence = NULL;
		dma_fence_put(fence);
		ret = 0;
	}
	spin_unlock_irqrestore(&fence->event_queue.lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_fence_cancel_callback);

/**
 * dma_fence_wait - Wait for a fence to be signaled.
 *
 * @fence: The fence to wait on
 * @interruptible: if true, do an interruptible wait
 * @timeout: timeout, in jiffies
 *
 * Returns -ENOENT if the fence has already passed.
 */
int dma_fence_wait(struct dma_fence *fence, bool interruptible, long timeout)
{
	unsigned long flags;
	int ret;

	if (WARN_ON(!fence))
		return -EINVAL;

	ret = check_signaling(fence);
	if (ret == -ENOENT) {
		spin_lock_irqsave(&fence->event_queue.lock, flags);
		signal_fence(fence);
		spin_unlock_irqrestore(&fence->event_queue.lock, flags);
		return ret;
	}

	if (ret)
		return ret;

	if (interruptible)
		ret = wait_event_interruptible_timeout(fence->event_queue,
				fence->signaled, timeout);
	else
		ret = wait_event_timeout(fence->event_queue,
				fence->signaled, timeout);

	return ret;
}
EXPORT_SYMBOL_GPL(dma_fence_wait);

int __dma_fence_wake_func(wait_queue_t *wait, unsigned mode,
		int flags, void *key)
{
	struct dma_fence_cb *cb =
			container_of(wait, struct dma_fence_cb, base);
	struct dma_fence *fence = cb->fence;
	int ret;

	ret = cb->func(cb, fence);
	cb->fence = NULL;
	dma_fence_put(fence);

	return ret;
}
EXPORT_SYMBOL_GPL(__dma_fence_wake_func);

/*
 * Helpers intended to be used by the ops of the dma_fence implementation:
 *
 * NOTE: helpers and fxns intended to be used by other dma-fence
 * implementations are not exported..  I'm not really sure if it makes
 * sense to have a dma-fence implementation that is itself a module.
 */

void __dma_fence_init(struct dma_fence *fence, struct dma_fence_ops *ops)
{
	WARN_ON(!ops || !ops->enable_signaling);

	kref_init(&fence->refcount);
	fence->ops = ops;
	init_waitqueue_head(&fence->event_queue);
}

/*
 * Pure sw implementation for dma-fence.  The CPU always gets involved.
 */

static int sw_enable_signaling(struct dma_fence *fence)
{
	/*
	 * pure sw, no irq's to enable, because the fence creator will
	 * always call dma_fence_signal()
	 */
	return 0;
}

static struct dma_fence_ops sw_fence_ops = {
		.enable_signaling = sw_enable_signaling,
};

/**
 * dma_fence_create - Create a simple sw-only fence.
 *
 * This fence only supports signaling from/to CPU.  Other implementations
 * of dma-fence can be used to support hardware to hardware signaling, if
 * supported by the hardware, and use the dma_fence_helper_* functions for
 * compatibility with other devices that only support sw signaling.
 */
struct dma_fence *dma_fence_create(void)
{
	struct dma_fence *fence;

	fence = kzalloc(sizeof(struct dma_fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	__dma_fence_init(fence, &sw_fence_ops);

	return fence;
}
EXPORT_SYMBOL_GPL(dma_fence_create);
