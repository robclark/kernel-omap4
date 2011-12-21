
#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"

#include <linux/dma-buf.h>

static struct sg_table *nouveau_gem_map_dma_buf(struct dma_buf_attachment *attachment,
					  enum dma_data_direction dir)
{
	struct nouveau_bo *nvbo = attachment->dmabuf->priv;
	struct drm_device *dev = nvbo->gem->dev;
	int npages = nvbo->bo.num_pages;
	struct sg_table *sg;

	mutex_lock(&dev->struct_mutex);
	sg = drm_prime_pages_to_sg(nvbo->bo.ttm->pages, npages);
	mutex_unlock(&dev->struct_mutex);
	return sg;
}

static void nouveau_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *sg)
{
	sg_free_table(sg);
	kfree(sg);
}

static void nouveau_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct nouveau_bo *nvbo = dma_buf->priv;

	if (nvbo->gem->export_dma_buf == dma_buf) {
		DRM_ERROR("unreference dmabuf %p\n", nvbo->gem);
		nvbo->gem->prime_fd = -1;
		nvbo->gem->export_dma_buf = NULL;
		drm_gem_object_unreference_unlocked(nvbo->gem);
	}
}

struct dma_buf_ops nouveau_dmabuf_ops =  {
	.map_dma_buf = nouveau_gem_map_dma_buf,
	.unmap_dma_buf = nouveau_gem_unmap_dma_buf,
	.release = nouveau_gem_dmabuf_release,
};

static int
nouveau_prime_new(struct drm_device *dev,
		  size_t size,
		  struct sg_table *sg,
                 struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *nvbo;
	u32 flags = 0;
	int ret;

	flags = TTM_PL_FLAG_TT;

	ret = nouveau_bo_new(dev, size, 0, flags, 0, 0,
			     sg, pnvbo);
	if (ret)
		return ret;
	nvbo = *pnvbo;

	/* we restrict allowed domains on nv50+ to only the types
	 * that were requested at creation time.  not possibly on
	 * earlier chips without busting the ABI.
	 */
	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_GART;
	nvbo->gem = drm_gem_object_alloc(dev, nvbo->bo.mem.size);
	if (!nvbo->gem) {
		nouveau_bo_ref(NULL, pnvbo);
		return -ENOMEM;
	}

//       nvbo->bo.persistant_swap_storage = nvbo->gem->filp;
	nvbo->gem->driver_private = nvbo;
	return 0;
}

int nouveau_gem_prime_handle_to_fd(struct drm_device *dev,
				   struct drm_file *file_priv,
				   uint32_t handle, int *prime_fd)
{
	struct nouveau_bo *nvbo;
	struct drm_gem_object *gem;
	int ret = 0;

	gem = drm_gem_object_lookup(dev, file_priv, handle);
	if (!gem)
		return -ENOENT;
	nvbo = nouveau_gem_object(gem);

	if (gem->prime_fd != -1) {
		/* drop lookup reference here already one on the fd */
		drm_gem_object_unreference_unlocked(gem);
		goto have_fd;
	}

	/* pin buffer into GTT */
	ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_TT);
	if (ret) {
		ret = -EINVAL;
		goto out_unlock;
	}

	gem->export_dma_buf = dma_buf_export(nvbo, &nouveau_dmabuf_ops, gem->size, 0600);
	if (IS_ERR(gem->export_dma_buf)) {
		ret = PTR_ERR(gem->export_dma_buf);
		goto out_unlock;
	}
	gem->prime_fd = dma_buf_fd(gem->export_dma_buf);

	/* don't drop reference since fd doesn't have it */
have_fd:
	*prime_fd = gem->prime_fd;
	return ret;
out_unlock:
	drm_gem_object_unreference_unlocked(gem);
	return ret;
}

int nouveau_gem_prime_fd_to_handle(struct drm_device *dev,
				   struct drm_file *file_priv,
				   int prime_fd, uint32_t *handle_p)
{
	struct nouveau_fpriv *fpriv = file_priv->driver_priv;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct nouveau_bo *nvbo;
	int ret;
	uint32_t handle;

	dma_buf = dma_buf_get(prime_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	ret = drm_prime_lookup_fd_handle_mapping(&fpriv->prime, dma_buf, &handle);
	if (!ret) {
		dma_buf_put(dma_buf);
		*handle_p = handle;
		return 0;
	}

	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto fail_put;
	}

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto fail_detach;
	}

	ret = nouveau_prime_new(dev, dma_buf->size, sg, &nvbo);
	if (ret)
		goto fail_unmap;

	nvbo->gem->import_attach = attach;
	ret = drm_gem_handle_create(file_priv, nvbo->gem, &handle);
	drm_gem_object_unreference_unlocked(nvbo->gem);
	if (ret)
		goto fail_unmap;

	ret = drm_prime_insert_fd_handle_mapping(&fpriv->prime, dma_buf, handle);
	if (ret)
		goto fail_handle;

	*handle_p = handle;
	return 0;

fail_handle:
	drm_gem_object_handle_unreference_unlocked(nvbo->gem);
fail_unmap:
	dma_buf_unmap_attachment(attach, sg);
fail_detach:
	dma_buf_detach(dma_buf, attach);
fail_put:
	dma_buf_put(dma_buf);
	return ret;
}

