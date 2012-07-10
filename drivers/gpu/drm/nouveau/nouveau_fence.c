/*
 * Copyright (C) 2007 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"

#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/dma-buf.h>

#include "nouveau_drv.h"
#include "nouveau_ramht.h"
#include "nouveau_fence.h"
#include "nouveau_software.h"
#include "nouveau_dma.h"
#include "nouveau_fifo.h"

int nouveau_fence_prime_init(struct drm_device *dev,
			     struct nouveau_fence_priv *priv, u32 align)
{
	int ret = 0;
#ifdef CONFIG_DMA_SHARED_BUFFER
	struct nouveau_fifo_priv *pfifo = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	u32 size = PAGE_ALIGN(pfifo->channels * align);

	mutex_init(&priv->prime_lock);
	priv->prime_align = align;
	ret = nouveau_bo_new(dev, size, 0, TTM_PL_FLAG_TT,
			     0, 0, NULL, &priv->prime_bo);
	if (ret)
		return ret;
	ret = nouveau_bo_map(priv->prime_bo);
	if (ret)
		goto err;

	ret = nouveau_gem_prime_export_bo(priv->prime_bo, 0400, size,
					  &priv->prime_buf);
	if (ret) {
		priv->prime_buf = NULL;
		nouveau_bo_unmap(priv->prime_bo);
		goto err;
	}
	return 0;

err:
	nouveau_bo_ref(NULL, &priv->prime_bo);
#endif
	return ret;
}

void nouveau_fence_prime_del(struct nouveau_fence_priv *priv)
{
	/* Our reference to prime_bo is released by freeing prime_buf */
	if (priv->prime_buf)
		dma_buf_put(priv->prime_buf);
	priv->prime_bo = NULL;

}

void
nouveau_fence_context_del(struct drm_device *dev,
			  struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence *fence, *fnext;
	struct nouveau_fence_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FENCE);

	spin_lock(&fctx->lock);
	list_for_each_entry_safe(fence, fnext, &fctx->pending, head) {
		if (fence->work)
			fence->work(fence->priv, false);
		fence->channel = NULL;
		list_del(&fence->head);
		nouveau_fence_unref(&fence);
	}
	spin_unlock(&fctx->lock);
	if (list_empty(&fctx->prime_sync_list))
		return;

	mutex_lock(&priv->prime_lock);
	while (!list_empty(&fctx->prime_sync_list)) {
		struct nouveau_fence_prime_bo_entry *entry;
		entry = list_first_entry(&fctx->prime_sync_list,
					 struct nouveau_fence_prime_bo_entry,
					 chan_entry);

		list_del(&entry->chan_entry);
		list_del(&entry->bo_entry);
		kfree(entry);
	}
	mutex_unlock(&priv->prime_lock);
}

void
nouveau_fence_context_new(struct nouveau_fence_chan *fctx)
{
	INIT_LIST_HEAD(&fctx->pending);
	spin_lock_init(&fctx->lock);
	INIT_LIST_HEAD(&fctx->prime_sync_list);
}

void
nouveau_fence_update(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FENCE);
	struct nouveau_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	struct nouveau_fence *fence, *fnext;

	spin_lock(&fctx->lock);
	list_for_each_entry_safe(fence, fnext, &fctx->pending, head) {
		if (priv->read(chan) < fence->sequence)
			break;

		if (fence->work)
			fence->work(fence->priv, true);
		fence->channel = NULL;
		list_del(&fence->head);
		nouveau_fence_unref(&fence);
	}
	spin_unlock(&fctx->lock);
}

int
nouveau_fence_emit(struct nouveau_fence *fence,
		   struct nouveau_channel *chan, bool prime)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FENCE);
	struct nouveau_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	int ret;

	fence->channel  = chan;
	fence->timeout  = jiffies + (3 * DRM_HZ);
	fence->sequence = ++fctx->sequence;

	ret = priv->emit(fence, prime);
	if (!ret) {
		kref_get(&fence->kref);
		spin_lock(&fctx->lock);
		list_add_tail(&fence->head, &fctx->pending);
		spin_unlock(&fctx->lock);
	}

	return ret;
}

bool
nouveau_fence_done(struct nouveau_fence *fence)
{
	if (fence->channel)
		nouveau_fence_update(fence->channel);
	return !fence->channel;
}

int
nouveau_fence_wait(struct nouveau_fence *fence, bool lazy, bool intr)
{
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	ktime_t t;
	int ret = 0;

	while (!nouveau_fence_done(fence)) {
		if (fence->timeout && time_after_eq(jiffies, fence->timeout)) {
			ret = -EBUSY;
			break;
		}

		__set_current_state(intr ? TASK_INTERRUPTIBLE :
					   TASK_UNINTERRUPTIBLE);
		if (lazy) {
			t = ktime_set(0, sleep_time);
			schedule_hrtimeout(&t, HRTIMER_MODE_REL);
			sleep_time *= 2;
			if (sleep_time > NSEC_PER_MSEC)
				sleep_time = NSEC_PER_MSEC;
		}

		if (intr && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	__set_current_state(TASK_RUNNING);
	return ret;
}

int
nouveau_fence_sync(struct nouveau_fence *fence, struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FENCE);
	struct nouveau_channel *prev;
	int ret = 0;

	prev = fence ? nouveau_channel_get_unlocked(fence->channel) : NULL;
	if (prev) {
		if (unlikely(prev != chan && !nouveau_fence_done(fence))) {
			ret = priv->sync(fence, prev, chan);
			if (unlikely(ret))
				ret = nouveau_fence_wait(fence, true, false);
		}
		nouveau_channel_put_unlocked(&prev);
	}

	return ret;
}

static int
nouveau_fence_prime_attach_sync(struct drm_device *dev,
				struct nouveau_fence_priv *priv,
				struct nouveau_bo *bo,
				struct dma_buf *sync_buf)
{
	struct dma_buf_attachment *attach;
	int ret;

	if (bo->sync_bo &&
	    sync_buf == bo->sync_bo->fence_import_attach->dmabuf)
		return 0;

	mutex_lock(&sync_buf->lock);
	list_for_each_entry(attach, &sync_buf->attachments, node) {
		if (attach->dev == dev->dev) {
			nouveau_bo_ref(attach->priv, &bo->sync_bo);
			mutex_unlock(&sync_buf->lock);
			return 0;
		}
	}
	mutex_unlock(&sync_buf->lock);

	nouveau_bo_ref(NULL, &bo->sync_bo);
	get_dma_buf(sync_buf);
	ret = nouveau_prime_import_bo(dev, sync_buf, &bo->sync_bo, 0);
	if (ret)
		dma_buf_put(sync_buf);
	return ret;
}

static int
nouveau_fence_prime_attach(struct nouveau_channel *chan,
			   struct nouveau_bo *bo,
			   struct dma_buf *sync_buf,
			   struct nouveau_fence_prime_bo_entry **pentry)
{
	struct nouveau_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	struct nouveau_fence_priv *priv;
	struct nouveau_fence_prime_bo_entry *entry;
	struct nouveau_bo *sync;
	int ret;

	/* new to chan or already existing */
	priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	ret = nouveau_fence_prime_attach_sync(chan->dev, priv, bo, sync_buf);
	if (ret)
		return ret;

	sync = bo->sync_bo;
	list_for_each_entry (entry, &sync->prime_chan_entries, bo_entry) {
		if (entry->chan == chan) {
			*pentry = entry;
			return 0;
		}
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->chan = chan;
	entry->bo = sync;
	ret = priv->prime_add_import(entry);
	if (!ret) {
		list_add_tail(&entry->chan_entry, &fctx->prime_sync_list);
		list_add_tail(&entry->bo_entry, &sync->prime_chan_entries);
		*pentry = entry;
	} else
		kfree(entry);
	return ret;
}

int nouveau_fence_sync_prime(struct nouveau_channel *chan,
			     struct dmabufmgr_validate *val)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FENCE);
	struct nouveau_fence_prime_bo_entry *e = NULL;
	int ret;

	if (!val->sync_buf)
		return 0;
	if (!priv || !priv->prime_sync ||
	    !priv->prime_add_import || !priv->prime_del_import)
		return -ENODEV;

	if (priv->prime_buf == val->sync_buf)
		return priv->prime_sync(chan, val->sync_buf->priv, val->sync_ofs, val->sync_val, 0);

	mutex_lock(&priv->prime_lock);
	ret = nouveau_fence_prime_attach(chan, val->priv,
					 val->sync_buf, &e);
	if (!ret)
		ret = priv->prime_sync(chan, e->bo, val->sync_ofs,
				       val->sync_val, e->sema_start);
	mutex_unlock(&priv->prime_lock);
	return ret;
}

int nouveau_fence_prime_get(struct nouveau_fence *fence,
			    struct dma_buf **sync_buf, u32 *ofs, u32 *val)
{
	struct drm_device *dev = fence->channel->dev;
	struct nouveau_fence_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FENCE);

	if (!priv->prime_sync)
		return -ENODEV;

	get_dma_buf(priv->prime_buf);
	*sync_buf = priv->prime_buf;
	*ofs = priv->prime_align * fence->channel->id;
	*val = fence->sequence;
	return 0;
}

static void
nouveau_fence_prime_del_import(struct nouveau_bo *nvbo)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(nvbo->bo.bdev);
	struct dma_buf_attachment *attach = nvbo->fence_import_attach;
	struct nouveau_fence_priv *priv;
	struct dma_buf *dma_buf;

	priv = (struct nouveau_fence_priv *)dev_priv->eng[NVOBJ_ENGINE_FENCE];

	while (!list_empty(&nvbo->prime_chan_entries)) {
		struct nouveau_fence_prime_bo_entry *entry;
		entry = list_first_entry(&nvbo->prime_chan_entries,
					 struct nouveau_fence_prime_bo_entry,
					 bo_entry);

		priv->prime_del_import(entry);
		list_del(&entry->chan_entry);
		list_del(&entry->bo_entry);
		kfree(entry);
	}

	dma_buf_unmap_attachment(attach, nvbo->bo.sg, DMA_BIDIRECTIONAL);
	dma_buf = attach->dmabuf;
	dma_buf_detach(attach->dmabuf, attach);
	dma_buf_put(dma_buf);
}


void nouveau_fence_prime_del_bo(struct nouveau_bo *nvbo)
{
	struct drm_nouveau_private *dev_priv = nouveau_bdev(nvbo->bo.bdev);
	struct nouveau_fence_priv *priv;
	priv = (struct nouveau_fence_priv *)dev_priv->eng[NVOBJ_ENGINE_FENCE];

	BUG_ON(!priv->prime_del_import);

	/* Impossible situation: we are a sync_bo synced to another
	 * sync bo?
	 */
	BUG_ON(nvbo->sync_bo && nvbo->fence_import_attach);

	if (nvbo->sync_bo) {
		mutex_lock(&priv->prime_lock);
		nouveau_bo_ref(NULL, &nvbo->sync_bo);
		mutex_unlock(&priv->prime_lock);
	}
	else if (nvbo->fence_import_attach)
		nouveau_fence_prime_del_import(nvbo);
}

static void
nouveau_fence_del(struct kref *kref)
{
	struct nouveau_fence *fence = container_of(kref, typeof(*fence), kref);
	kfree(fence);
}

void
nouveau_fence_unref(struct nouveau_fence **pfence)
{
	if (*pfence)
		kref_put(&(*pfence)->kref, nouveau_fence_del);
	*pfence = NULL;
}

struct nouveau_fence *
nouveau_fence_ref(struct nouveau_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

int
nouveau_fence_new(struct nouveau_channel *chan,
		  struct nouveau_fence **pfence, bool prime)
{
	struct nouveau_fence *fence;
	int ret = 0;

	if (unlikely(!chan->engctx[NVOBJ_ENGINE_FENCE]))
		return -ENODEV;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;
	kref_init(&fence->kref);

	if (chan) {
		ret = nouveau_fence_emit(fence, chan, prime);
		if (ret)
			nouveau_fence_unref(&fence);
	}

	*pfence = fence;
	return ret;
}
