/*
 * dma-fence implementation that supports hw synchronization via hw
 * read/write of memory semaphore
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

#ifndef __DMA_BIKESHED_FENCE_H__
#define __DMA_BIKESHED_FENCE_H__

#include <linux/types.h>
#include <linux/dma-fence.h>
#include <linux/dma-buf.h>

struct dma_bikeshed_fence {
	struct dma_fence base;

	struct dma_buf *sync_buf;
	uint32_t seqno_ofs;
	uint32_t seqno;

	int (*enable_signaling)(struct dma_bikeshed_fence *fence);
};

/*
 * TODO does it make sense to be able to enable dma-fence without dma-buf,
 * or visa versa?
 */
#ifdef CONFIG_DMA_SHARED_BUFFER

extern struct dma_fence_ops bikeshed_fence_ops;

static inline bool is_bikeshed_fence(struct dma_fence *fence)
{
	return fence->ops == &bikeshed_fence_ops;
}

static inline struct dma_bikeshed_fence *to_bikeshed_fence(struct dma_fence *fence)
{
	WARN_ON(!is_bikeshed_fence(fence));
	return container_of(fence, struct dma_bikeshed_fence, base);
}

struct dma_bikeshed_fence *dma_bikeshed_fence_create(struct dma_buf *sync_buf,
		uint32_t seqno_ofs, uint32_t seqno,
		int (*enable_signaling)(struct dma_bikeshed_fence *fence));

#else
// TODO
#endif /* CONFIG_DMA_SHARED_BUFFER */

#endif /* __DMA_BIKESHED_FENCE_H__ */
