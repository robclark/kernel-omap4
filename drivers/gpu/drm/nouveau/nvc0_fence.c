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

struct nvc0_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
};

struct nvc0_fence_chan {
	struct nouveau_fence_chan base;
	struct nouveau_vma vma;
	struct nouveau_vma prime_vma;
};

static int
nvc0_fence_emit(struct nouveau_fence *fence, bool prime)
{
	struct nouveau_channel *chan = fence->channel;
	struct nvc0_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	u64 addr = fctx->vma.offset + chan->id * 16;
	int ret, i;

	ret = RING_SPACE(chan, prime ? 10 : 5);
	if (ret)
		return ret;

	for (i = 0; i < (prime ? 2 : 1); ++i) {
		if (i)
			addr = fctx->prime_vma.offset + chan->id * 16;
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr));
		OUT_RING  (chan, lower_32_bits(addr));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG);
	}
	FIRE_RING(chan);
	return 0;
}

static int
nvc0_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nvc0_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	u64 addr = fctx->vma.offset + prev->id * 16;
	int ret;

	ret = RING_SPACE(chan, 5);
	if (ret == 0) {
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr));
		OUT_RING  (chan, lower_32_bits(addr));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL |
				 NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD);
		FIRE_RING (chan);
	}

	return ret;
}

static u32
nvc0_fence_read(struct nouveau_channel *chan)
{
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	return nouveau_bo_rd32(priv->bo, chan->id * 16/4);
}

static void
nvc0_fence_context_del(struct nouveau_channel *chan, int engine)
{
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, engine);
	struct nvc0_fence_chan *fctx = chan->engctx[engine];

	if (priv->base.prime_bo)
		nouveau_bo_vma_del(priv->base.prime_bo, &fctx->prime_vma);
	nouveau_bo_vma_del(priv->bo, &fctx->vma);
	nouveau_fence_context_del(chan->dev, &fctx->base);
	chan->engctx[engine] = NULL;
	kfree(fctx);
}

static int
nvc0_fence_context_new(struct nouveau_channel *chan, int engine)
{
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, engine);
	struct nvc0_fence_chan *fctx;
	int ret;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);

	ret = nouveau_bo_vma_add(priv->bo, chan->vm, &fctx->vma);
	if (!ret && priv->base.prime_bo)
		ret = nouveau_bo_vma_add(priv->base.prime_bo, chan->vm,
					 &fctx->prime_vma);
	if (ret)
		nvc0_fence_context_del(chan, engine);

	fctx->base.sequence = nouveau_bo_rd32(priv->bo, chan->id * 16/4);
	if (priv->base.prime_bo)
		nouveau_bo_wr32(priv->base.prime_bo, chan->id * 16/4,
				fctx->base.sequence);
	return ret;
}

static int
nvc0_fence_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static int
nvc0_fence_init(struct drm_device *dev, int engine)
{
	return 0;
}

static void
nvc0_fence_destroy(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fence_priv *priv = nv_engine(dev, engine);

	nouveau_fence_prime_del(&priv->base);
	nouveau_bo_unmap(priv->bo);
	nouveau_bo_unpin(priv->bo);
	nouveau_bo_ref(NULL, &priv->bo);
	dev_priv->eng[engine] = NULL;
	kfree(priv);
}

static int
nvc0_fence_prime_sync(struct nouveau_channel *chan,
		      struct nouveau_bo *bo,
		      u32 ofs, u32 val, u64 sema_start)
{
	struct nvc0_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	int ret = RING_SPACE(chan, 5);
	if (ret)
		return ret;

	if (bo == priv->base.prime_bo)
		sema_start = fctx->prime_vma.offset;
	else
		NV_ERROR(chan->dev, "syncing with %08Lx + %08x >= %08x\n",
			sema_start, ofs, val);
	sema_start += ofs;

	BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
	OUT_RING  (chan, upper_32_bits(sema_start));
	OUT_RING  (chan, lower_32_bits(sema_start));
	OUT_RING  (chan, val);
	OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL |
			 NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD);
	FIRE_RING (chan);
	return ret;
}

static void
nvc0_fence_prime_del_import(struct nouveau_fence_prime_bo_entry *entry) {
	nouveau_bo_vma_del(entry->bo, &entry->vma);
}

static int
nvc0_fence_prime_add_import(struct nouveau_fence_prime_bo_entry *entry) {
	int ret = nouveau_bo_vma_add_access(entry->bo, entry->chan->vm,
					    &entry->vma, NV_MEM_ACCESS_RO);
	entry->sema_start = entry->vma.offset;
	return ret;
}

int
nvc0_fence_create(struct drm_device *dev)
{
	struct nouveau_fifo_priv *pfifo = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fence_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.engine.destroy = nvc0_fence_destroy;
	priv->base.engine.init = nvc0_fence_init;
	priv->base.engine.fini = nvc0_fence_fini;
	priv->base.engine.context_new = nvc0_fence_context_new;
	priv->base.engine.context_del = nvc0_fence_context_del;
	priv->base.emit = nvc0_fence_emit;
	priv->base.sync = nvc0_fence_sync;
	priv->base.read = nvc0_fence_read;
	dev_priv->eng[NVOBJ_ENGINE_FENCE] = &priv->base.engine;

	priv->base.prime_sync = nvc0_fence_prime_sync;
	priv->base.prime_add_import = nvc0_fence_prime_add_import;
	priv->base.prime_del_import = nvc0_fence_prime_del_import;

	ret = nouveau_bo_new(dev, 16 * pfifo->channels, 0, TTM_PL_FLAG_VRAM,
			     0, 0, NULL, &priv->bo);
	if (ret)
		goto err;
	ret = nouveau_bo_pin(priv->bo, TTM_PL_FLAG_VRAM);
	if (ret)
		goto err_ref;

	ret = nouveau_bo_map(priv->bo);
	if (ret)
		goto err_unpin;

	ret = nouveau_fence_prime_init(dev, &priv->base, 16);
	if (ret)
		goto err_unmap;
	return 0;

err_unmap:
	nouveau_bo_unmap(priv->bo);
err_unpin:
	nouveau_bo_unpin(priv->bo);
err_ref:
	nouveau_bo_ref(NULL, &priv->bo);
err:
	dev_priv->eng[NVOBJ_ENGINE_FENCE] = NULL;
	kfree(priv);
	return ret;
}
