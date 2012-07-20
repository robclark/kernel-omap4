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

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/dma-bikeshed-fence.h>

static int enable_signaling(struct dma_fence *fence)
{
	struct dma_bikeshed_fence *bikeshed_fence = to_bikeshed_fence(fence);
	return bikeshed_fence->enable_signaling(bikeshed_fence);
}

struct dma_fence_ops bikeshed_fence_ops = {
		.enable_signaling = enable_signaling,
};

/**
 * dma_bikeshed_fence_create - Create a hw sync fence.
 *
 * @sync_buf: buffer containing the memory location to signal on
 * @seqno_ofs: the offset within @sync_buf
 * @seqn: the sequence # to signal on
 * @enable_signaling: callback which is called when some other device is
 *    waiting for sw notification of fence
 */
struct dma_bikeshed_fence *dma_bikeshed_fence_create(struct dma_buf *sync_buf,
		uint32_t seqno_ofs, uint32_t seqno,
		int (*enable_signaling)(struct dma_bikeshed_fence *fence))
{
	struct dma_bikeshed_fence *fence;

	if (WARN_ON(!sync_buf) || WARN_ON(!enable_signaling))
		return ERR_PTR(-EINVAL);

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	__dma_fence_init(&fence->base, &bikeshed_fence_ops);

	fence->sync_buf = sync_buf; // hmm, this should take a ref and we should have a destructor to drop ref
	fence->seqno_ofs = seqno_ofs;
	fence->seqno = seqno;
	fence->enable_signaling = enable_signaling;

	return fence;
}
EXPORT_SYMBOL_GPL(dma_bikeshed_fence_create);
