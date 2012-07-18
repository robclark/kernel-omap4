/*
 * Copyright (C) 2012 Canonical Ltd
 *
 * Based on ttm_bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */


#include <linux/dma-buf-mgr.h>
#include <linux/export.h>
#include <linux/sched.h>

static void dmabufmgr_backoff_reservation_locked(struct list_head *list)
{
	struct dmabufmgr_validate *entry;

	list_for_each_entry(entry, list, head) {
		struct dma_buf *bo = entry->bo;
		if (!entry->reserved)
			continue;
		entry->reserved = false;

		entry->num_fences = 0;

		atomic_set(&bo->reserved, 0);
		wake_up_all(&bo->event_queue);
	}
}

static int
dmabufmgr_wait_unreserved_locked(struct list_head *list,
				    struct dma_buf *bo)
{
	int ret;

	spin_unlock(&dma_buf_reserve_lock);
	ret = dma_buf_wait_unreserved(bo, true);
	spin_lock(&dma_buf_reserve_lock);
	if (unlikely(ret != 0))
		dmabufmgr_backoff_reservation_locked(list);
	return ret;
}

void
dmabufmgr_backoff_reservation(struct list_head *list)
{
	if (list_empty(list))
		return;

	spin_lock(&dma_buf_reserve_lock);
	dmabufmgr_backoff_reservation_locked(list);
	spin_unlock(&dma_buf_reserve_lock);
}
EXPORT_SYMBOL_GPL(dmabufmgr_backoff_reservation);

int
dmabufmgr_reserve_buffers(struct list_head *list)
{
	struct dmabufmgr_validate *entry;
	int ret;
	u32 val_seq;

	if (list_empty(list))
		return 0;

	list_for_each_entry(entry, list, head) {
		entry->reserved = false;
		entry->num_fences = 0;
	}

retry:
	spin_lock(&dma_buf_reserve_lock);
	val_seq = atomic_inc_return(&dma_buf_reserve_counter);

	list_for_each_entry(entry, list, head) {
		struct dma_buf *bo = entry->bo;

retry_this_bo:
		ret = dma_buf_reserve_locked(bo, true, true, true, val_seq);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
			ret = dmabufmgr_wait_unreserved_locked(list, bo);
			if (unlikely(ret != 0)) {
				spin_unlock(&dma_buf_reserve_lock);
				return ret;
			}
			goto retry_this_bo;
		case -EAGAIN:
			dmabufmgr_backoff_reservation_locked(list);
			spin_unlock(&dma_buf_reserve_lock);
			ret = dma_buf_wait_unreserved(bo, true);
			if (unlikely(ret != 0))
				return ret;
			goto retry;
		default:
			dmabufmgr_backoff_reservation_locked(list);
			spin_unlock(&dma_buf_reserve_lock);
			return ret;
		}

		entry->reserved = true;

		if (entry->shared &&
		    bo->fence_shared_count == DMA_BUF_MAX_SHARED_FENCE) {
			WARN_ON_ONCE(1);
			dmabufmgr_backoff_reservation_locked(list);
			spin_unlock(&dma_buf_reserve_lock);
			return -EINVAL;
		}

		if (!entry->shared && bo->fence_shared_count) {
			entry->num_fences = bo->fence_shared_count;
			BUILD_BUG_ON(sizeof(entry->fences) != sizeof(bo->fence_shared));
			memcpy(entry->fences, bo->fence_shared, sizeof(bo->fence_shared));
		} else if (bo->fence_excl) {
			entry->num_fences = 1;
			entry->fences[0] = bo->fence_excl;
		} else
			entry->num_fences = 0;
	}
	spin_unlock(&dma_buf_reserve_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(dmabufmgr_reserve_buffers);

static int
dmabufmgr_wait_single(struct dmabufmgr_validate *val, bool intr, bool lazy, unsigned long timeout)
{
	int ret = 0;
#if 0
	uint32_t *map, *seq, ofs;
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	size_t start;

	if (!val->num_fences)
		return 0;

	start = val->sync_ofs & PAGE_MASK;
	ofs = val->sync_ofs & ~PAGE_MASK;

	ret = dma_buf_begin_cpu_access(val->sync_buf, start,
				       start + PAGE_SIZE,
				       DMA_FROM_DEVICE);
	if (ret)
		return ret;

	map = dma_buf_kmap(val->sync_buf, val->sync_ofs >> PAGE_SHIFT);
	seq = &map[ofs/4];

	while (1) {
		val->retval = *seq;
		if (val->retval - val->sync_val < 0x80000000U)
			break;

		if (time_after_eq(jiffies, timeout)) {
			ret = -EBUSY;
			break;
		}

		set_current_state(intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);

		if (lazy) {
			ktime_t t = ktime_set(0, sleep_time);
			schedule_hrtimeout(&t, HRTIMER_MODE_REL);
			if (sleep_time < NSEC_PER_MSEC)
				sleep_time *= 2;
		} else
			cpu_relax();

		if (intr && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	set_current_state(TASK_RUNNING);

	dma_buf_kunmap(val->sync_buf, val->sync_ofs >> PAGE_SHIFT, map);
	dma_buf_end_cpu_access(val->sync_buf, start,
			       start + PAGE_SIZE,
			       DMA_FROM_DEVICE);

	val->waited = !ret;
	if (!ret) {
		dma_buf_put(val->sync_buf);
		val->sync_buf = NULL;
	}
#endif
	return ret;
}

int
dmabufmgr_wait_completed_cpu(struct list_head *list, bool intr, bool lazy)
{
	struct dmabufmgr_validate *entry;
	unsigned long timeout = jiffies + 4 * HZ;
	int ret;

	list_for_each_entry(entry, list, head) {
		ret = dmabufmgr_wait_single(entry, intr, lazy, timeout);
		if (ret && ret != -ERESTARTSYS)
			pr_err("waiting returns %i\n", ret);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(dmabufmgr_wait_completed_cpu);

void
dmabufmgr_fence_buffer_objects(struct dma_fence *fence, struct list_head *list)
{
	struct dmabufmgr_validate *entry;
	struct dma_buf *bo;

	if (list_empty(list) || WARN_ON(!fence))
		return;

	/* Until deferred fput hits mainline, release old things here */
	list_for_each_entry(entry, list, head) {
		bo = entry->bo;

		if (!entry->shared) {
			int i;
			for (i = 0; i < bo->fence_shared_count; ++i) {
				dma_fence_put(bo->fence_shared[i]);
				bo->fence_shared[i] = NULL;
			}
			bo->fence_shared_count = 0;
			if (bo->fence_excl) {
				dma_fence_put(bo->fence_excl);
				bo->fence_excl = NULL;
			}
		}

		entry->reserved = false;
	}

	spin_lock(&dma_buf_reserve_lock);

	list_for_each_entry(entry, list, head) {
		bo = entry->bo;

		dma_fence_get(fence);
		if (entry->shared)
			bo->fence_shared[bo->fence_shared_count++] = fence;
		else
			bo->fence_excl = fence;

		dma_buf_unreserve_locked(bo);
	}

	spin_unlock(&dma_buf_reserve_lock);
}
EXPORT_SYMBOL_GPL(dmabufmgr_fence_buffer_objects);
