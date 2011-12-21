#include "drmP.h"
#include "i915_drv.h"
#include <linux/dma-buf.h>

struct sg_table *i915_gem_map_dma_buf(struct dma_buf_attachment *attachment,
				      enum dma_data_direction dir)
					  
{
	struct drm_i915_gem_object *obj = attachment->dmabuf->priv;
	struct drm_device *dev = obj->base.dev;
	int npages = obj->base.size / PAGE_SIZE;
	struct sg_table *sg = NULL;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return NULL;

	if (!obj->pages) {
		ret = i915_gem_object_get_pages_gtt(obj, __GFP_NORETRY | __GFP_NOWARN);
		if (ret)
			goto out;
	}

	sg = drm_prime_pages_to_sg(obj->pages, npages);
out:
	mutex_unlock(&dev->struct_mutex);
	return sg;
}

void i915_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
			    struct sg_table *sg)
{
	sg_free_table(sg);
	kfree(sg);
}

void i915_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct drm_i915_gem_object *obj = dma_buf->priv;

	if (obj->base.export_dma_buf == dma_buf) {
		/* drop the reference on the export fd holds */
		obj->base.prime_fd = -1;
		obj->base.export_dma_buf = NULL;
		drm_gem_object_unreference_unlocked(&obj->base);
	}
}

struct dma_buf_ops i915_dmabuf_ops =  {
	.map_dma_buf = i915_gem_map_dma_buf,
	.unmap_dma_buf = i915_gem_unmap_dma_buf,
	.release = i915_gem_dmabuf_release,
};

int i915_gem_prime_handle_to_fd(struct drm_device *dev,
				struct drm_file *file_priv,
				uint32_t handle, int *prime_fd)
{
	struct drm_i915_gem_object *obj;
	int ret;
	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	obj = to_intel_bo(drm_gem_object_lookup(dev, file_priv, handle));
	if (!obj) {
		ret = -EBADF;
		goto unlock;
	}

	if (obj->base.prime_fd != -1) {
		/* drop reference since we have a prime fd already referencing
		   the object */
		drm_gem_object_unreference(&obj->base);
		goto have_fd;
	}

	obj->base.export_dma_buf = dma_buf_export(obj, &i915_dmabuf_ops,
						  obj->base.size, 0600);
	if (IS_ERR(obj->base.export_dma_buf)) {
		ret = PTR_ERR(obj->base.export_dma_buf);
		goto unlock;
	}
	obj->base.prime_fd = dma_buf_fd(obj->base.export_dma_buf);

	/* leave the lookup reference with the fd */

have_fd:
	*prime_fd = obj->base.prime_fd;
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int i915_gem_prime_fd_to_handle(struct drm_device *dev,
				struct drm_file *file,
				int prime_fd, uint32_t *handle)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct drm_i915_gem_object *obj;
	int npages;
	int size;
	struct scatterlist *iter;
	int ret;
	int i;

	dma_buf = dma_buf_get(prime_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	ret = drm_prime_lookup_fd_handle_mapping(&file_priv->prime, dma_buf, handle);
	if (!ret) {
		/* drop reference we got above */
		dma_buf_put(dma_buf);
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

	size = dma_buf->size;
	npages = size / PAGE_SIZE;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL) {
		ret = -ENOMEM;
		goto fail_unmap;
	}

	ret = drm_gem_private_object_init(dev, &obj->base, size);
	if (ret) {
		ret = -ENOMEM;
		kfree(obj);
		goto fail_unmap;
	}

	obj->pages = drm_malloc_ab(npages, sizeof(struct page *));
	if (obj->pages == NULL) {
		DRM_ERROR("obj pages is NULL %d\n", npages);
		return -ENOMEM;
	}

	for_each_sg(sg->sgl, iter, npages, i)
		obj->pages[i] = sg_page(iter);

	obj->base.import_attach = attach;
	obj->sg = sg;
	ret = drm_gem_handle_create(file, &obj->base, handle);
	if (ret) {
		drm_gem_object_release(&obj->base);
//		i915_gem_info_remove_obj(dev->dev_private, obj->base.size);
		kfree(obj);
		return ret;
	}

	ret = drm_prime_insert_fd_handle_mapping(&file_priv->prime, dma_buf, *handle);
	if (ret == 0)
		goto fail_handle;
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference(&obj->base);
	return 0;

fail_handle:
	drm_gem_object_handle_unreference_unlocked(&obj->base);
fail_unmap:
	dma_buf_unmap_attachment(attach, sg);
fail_detach:
	dma_buf_detach(dma_buf, attach);
fail_put:
	dma_buf_put(dma_buf);
	return ret;
}
