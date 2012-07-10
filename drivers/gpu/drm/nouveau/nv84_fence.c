/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fifo.h"
#include "nouveau_ramht.h"
#include "nouveau_fence.h"

struct nv84_fence_chan {
	struct nouveau_fence_chan base;
	u32 sema_start;
};

struct nv84_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_gpuobj *mem;
};

static int
nv84_fence_emit(struct nouveau_fence *fence, bool prime)
{
	struct nouveau_channel *chan = fence->channel;
	int i, ret;

	ret = RING_SPACE(chan, prime ? 14 : 7);
	if (ret)
		return ret;

	for (i = 0; i < (prime ? 2 : 1); ++i) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, i ? NvSemaPrime : NvSema);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(chan->id * 16));
		OUT_RING  (chan, lower_32_bits(chan->id * 16));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG);
	}
	FIRE_RING (chan);
	return ret;
}

static int
nv84_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	int ret = RING_SPACE(chan, 7);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, NvSema);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(prev->id * 16));
		OUT_RING  (chan, lower_32_bits(prev->id * 16));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL);
		FIRE_RING (chan);
	}
	return ret;
}

static u32
nv84_fence_read(struct nouveau_channel *chan)
{
	struct nv84_fence_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	return nv_ro32(priv->mem, chan->id * 16);
}

static int
nv84_fence_prime_sync(struct nouveau_channel *chan,
		      struct nouveau_bo *bo,
		      u32 ofs, u32 val, u64 sema_start)
{
	struct nv84_fence_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	int ret = RING_SPACE(chan, 7);
	u32 sema = 0;
	if (ret < 0)
		return ret;

	if (bo == priv->base.prime_bo) {
		sema = NvSema;
	} else {
		struct sg_table *sgt = bo->bo.sg;
		struct scatterlist *sg;
		u32 i;
		sema = sema_start;
		for_each_sg(sgt->sgl, sg, sgt->nents, i) {
			if (ofs < sg->offset + sg->length) {
				ofs -= sg->offset;
				break;
			}
			sema++;
		}
	}

	BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
	OUT_RING  (chan, sema);
	BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
	OUT_RING  (chan, 0);
	OUT_RING  (chan, ofs);
	OUT_RING  (chan, val);
	OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL);
	FIRE_RING (chan);
	return ret;
}

static void
nv84_fence_prime_del_import(struct nouveau_fence_prime_bo_entry *entry) {
	u32 i;
	for (i = entry->sema_start; i <  entry->sema_start + entry->sema_len; ++i)
		nouveau_ramht_remove(entry->chan, i);
}

static int
nv84_fence_prime_add_import(struct nouveau_fence_prime_bo_entry *entry) {
	struct sg_table *sgt = entry->bo->bo.sg;
	struct nouveau_channel *chan = entry->chan;
	struct nv84_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	struct scatterlist *sg;
	u32 i, sema;
	int ret;

	sema = entry->sema_start = fctx->sema_start;
	entry->sema_len = 0;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		struct nouveau_gpuobj *obj;
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_FROM_MEMORY,
					     sg_dma_address(sg), PAGE_SIZE,
					     NV_MEM_ACCESS_RO,
					     NV_MEM_TARGET_PCI, &obj);
		if (ret)
			goto err;

		ret = nouveau_ramht_insert(chan, sema, obj);
		nouveau_gpuobj_ref(NULL, &obj);
		if (ret)
			goto err;
		entry->sema_len++;
		sema++;
	}
	fctx->sema_start += (entry->sema_len + 0xff) & ~0xff;
	return 0;

err:
	nv84_fence_prime_del_import(entry);
	return ret;
}

static void
nv84_fence_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv84_fence_chan *fctx = chan->engctx[engine];
	nouveau_fence_context_del(chan->dev, &fctx->base);
	chan->engctx[engine] = NULL;

	kfree(fctx);
}

static int
nv84_fence_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv84_fence_priv *priv = nv_engine(chan->dev, engine);
	struct nv84_fence_chan *fctx;
	struct nouveau_gpuobj *obj;
	int ret;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);
	fctx->sema_start = 0xc0000000 | (chan->id << 20);

	ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_FROM_MEMORY,
				     priv->mem->vinst, priv->mem->size,
				     NV_MEM_ACCESS_RW,
				     NV_MEM_TARGET_VRAM, &obj);
	if (ret == 0) {
		ret = nouveau_ramht_insert(chan, NvSema, obj);
		nouveau_gpuobj_ref(NULL, &obj);
		fctx->base.sequence = nv_ro32(priv->mem, chan->id * 16);
	}

	if (priv->base.prime_bo) {
		struct nouveau_mem *mem = priv->base.prime_bo->bo.mem.mm_node;
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_FROM_MEMORY,
					    mem->pages[0], PAGE_SIZE,
					    NV_MEM_ACCESS_RW,
					    NV_MEM_TARGET_PCI, &obj);
		if (ret == 0) {
			ret = nouveau_ramht_insert(chan, NvSemaPrime, obj);
			nouveau_gpuobj_ref(NULL, &obj);
			nouveau_bo_wr32(priv->base.prime_bo, chan->id * 4,
					fctx->base.sequence);
		}
	}

	if (ret)
		nv84_fence_context_del(chan, engine);
	return ret;
}

static int
nv84_fence_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static int
nv84_fence_init(struct drm_device *dev, int engine)
{
	return 0;
}

static void
nv84_fence_destroy(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv84_fence_priv *priv = nv_engine(dev, engine);

	nouveau_fence_prime_del(&priv->base);
	nouveau_gpuobj_ref(NULL, &priv->mem);
	dev_priv->eng[engine] = NULL;
	kfree(priv);
}

int
nv84_fence_create(struct drm_device *dev)
{
	struct nouveau_fifo_priv *pfifo = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv84_fence_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.engine.destroy = nv84_fence_destroy;
	priv->base.engine.init = nv84_fence_init;
	priv->base.engine.fini = nv84_fence_fini;
	priv->base.engine.context_new = nv84_fence_context_new;
	priv->base.engine.context_del = nv84_fence_context_del;
	priv->base.emit = nv84_fence_emit;
	priv->base.sync = nv84_fence_sync;
	priv->base.read = nv84_fence_read;

	priv->base.prime_sync = nv84_fence_prime_sync;
	priv->base.prime_add_import = nv84_fence_prime_add_import;
	priv->base.prime_del_import = nv84_fence_prime_del_import;
	dev_priv->eng[NVOBJ_ENGINE_FENCE] = &priv->base.engine;

	ret = nouveau_gpuobj_new(dev, NULL, 16 * pfifo->channels,
				 0x1000, 0, &priv->mem);
	if (ret)
		goto out;

	ret = nouveau_fence_prime_init(dev, &priv->base, 16);

out:
	if (ret)
		nv84_fence_destroy(dev, NVOBJ_ENGINE_FENCE);
	return ret;
}
