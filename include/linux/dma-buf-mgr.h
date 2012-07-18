/*
 * Header file for dma buffer sharing framework.
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Author: Sumit Semwal <sumit.semwal@ti.com>
 *
 * Many thanks to linaro-mm-sig list, and specially
 * Arnd Bergmann <arnd@arndb.de>, Rob Clark <rob@ti.com> and
 * Daniel Vetter <daniel@ffwll.ch> for their support in creation and
 * refining of this idea.
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
#ifndef __DMA_BUF_MGR_H__
#define __DMA_BUF_MGR_H__

#include <linux/dma-buf.h>
#include <linux/list.h>

/** based on ttm_execbuf_util
 */
struct dmabufmgr_validate {
	/* Input parameters, set before reserve */
	struct list_head head;
	struct dma_buf *bo;
	void *priv;
	bool shared;

	/* for internal use */
	bool reserved;
	/* fences to wait on, if shared, last exclusive fence
	 * if exclusive, last shared fences (if non-null), or
	 * last exclusive fence
	 */
	unsigned num_fences;
	struct dma_fence *fences[DMA_BUF_MAX_SHARED_FENCE];
};

#ifdef CONFIG_DMA_SHARED_BUFFER

/** reserve a linked list of struct dmabufmgr_validate entries */
extern int
dmabufmgr_reserve_buffers(struct list_head *list);

/** Undo reservation */
extern void
dmabufmgr_backoff_reservation(struct list_head *list);

/** Commit reservation */
extern void
dmabufmgr_fence_buffer_objects(struct dma_fence *fence, struct list_head *list);

/** Wait for completion on cpu
 * intr: interruptible wait
 * lazy: try once every tick instead of busywait
 */
extern int
dmabufmgr_wait_completed_cpu(struct list_head *list, bool intr, bool lazy);

#else /* CONFIG_DMA_SHARED_BUFFER */

/** reserve a linked list of struct dmabufmgr_validate entries */
static inline int
dmabufmgr_reserve_buffers(struct list_head *list)
{
	return list_empty(list) ? 0 : -ENODEV;
}

/** Undo reservation */
static inline void
dmabufmgr_backoff_reservation(struct list_head *list)
{}

/** Commit reservation */
static inline void
dmabufmgr_fence_buffer_objects(struct dma_fence *fence, struct list_head *list)
{}

static inline int
dmabufmgr_wait_completed_cpu(struct list_head *list, bool intr, bool lazy)
{
	return list_empty(list) ? 0 : -ENODEV;
}

#endif /* CONFIG_DMA_SHARED_BUFFER */

#endif /* __DMA_BUF_MGR_H__ */
