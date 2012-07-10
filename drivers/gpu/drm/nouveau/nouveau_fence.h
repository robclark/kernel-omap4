#ifndef __NOUVEAU_FENCE_H__
#define __NOUVEAU_FENCE_H__

#include <linux/dma-buf-mgr.h>

struct nouveau_fence {
	struct list_head head;
	struct kref kref;

	struct nouveau_channel *channel;
	unsigned long timeout;
	u32 sequence;

	void (*work)(void *priv, bool signalled);
	void *priv;
};

int  nouveau_fence_new(struct nouveau_channel *,
		       struct nouveau_fence **,
		       bool prime);

struct nouveau_fence *
nouveau_fence_ref(struct nouveau_fence *);
void nouveau_fence_unref(struct nouveau_fence **);

int  nouveau_fence_emit(struct nouveau_fence *,
			struct nouveau_channel *, bool prime);
bool nouveau_fence_done(struct nouveau_fence *);
int  nouveau_fence_wait(struct nouveau_fence *, bool lazy, bool intr);
int  nouveau_fence_sync(struct nouveau_fence *, struct nouveau_channel *);
int nouveau_fence_sync_prime(struct nouveau_channel *,
			     struct dmabufmgr_validate *);
void nouveau_fence_idle(struct nouveau_channel *);
void nouveau_fence_update(struct nouveau_channel *);
int nouveau_fence_prime_get(struct nouveau_fence *fence,
			    struct dma_buf **sync_buf, u32 *ofs, u32 *val);
void nouveau_fence_prime_del_bo(struct nouveau_bo *bo);

struct nouveau_fence_chan {
	struct list_head pending;
	spinlock_t lock;
	u32 sequence;
	struct list_head prime_sync_list;
};

struct nouveau_fence_prime_bo_entry {
	struct list_head bo_entry;
	struct list_head chan_entry;
	struct nouveau_bo *bo;
	struct nouveau_channel *chan;

	u64 sema_start, sema_len;
	struct nouveau_vma vma;
};

struct nouveau_fence_priv {
	struct nouveau_exec_engine engine;
	int (*emit)(struct nouveau_fence *, bool prime);
	int (*sync)(struct nouveau_fence *, struct nouveau_channel *,
		    struct nouveau_channel *);
	u32 (*read)(struct nouveau_channel *);
	int (*prime_sync)(struct nouveau_channel *chan, struct nouveau_bo *bo,
			  u32 ofs, u32 val, u64 sema_start);
	int (*prime_add_import)(struct nouveau_fence_prime_bo_entry *);
	void (*prime_del_import)(struct nouveau_fence_prime_bo_entry *);

	struct mutex prime_lock;
	struct dma_buf *prime_buf;
	struct nouveau_bo *prime_bo;
	u32 prime_align;
};

int nouveau_fence_prime_init(struct drm_device *,
			     struct nouveau_fence_priv *, u32 align);
void nouveau_fence_prime_del(struct nouveau_fence_priv *priv);

void nouveau_fence_context_new(struct nouveau_fence_chan *);
void nouveau_fence_context_del(struct drm_device *,
			       struct nouveau_fence_chan *);

int nv04_fence_create(struct drm_device *dev);
int nv04_fence_mthd(struct nouveau_channel *, u32, u32, u32);

int nv10_fence_create(struct drm_device *dev);
int nv84_fence_create(struct drm_device *dev);
int nvc0_fence_create(struct drm_device *dev);

#endif
