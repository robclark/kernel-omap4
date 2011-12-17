/*
 * drivers/staging/omapdrm/omap_gem_dmabuf.c
 *
 * Copyright (C) 2011 Texas Instruments
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

#include "omap_drv.h"

#include <linux/dma-buf.h>

struct sg_table *omap_gem_map_dma_buf(
		struct dma_buf_attachment *attachment,
		enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attachment->dmabuf->priv;
	struct sg_table *sg;
	dma_addr_t paddr;
	int ret;

DBG("**** map: %p", obj);

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	/* camera, etc, need physically contiguous.. but we need a
	 * better way to know this..
	 */
	ret = omap_gem_get_paddr(obj, &paddr, true);
	if (ret)
		goto out;

	ret = sg_alloc_table(sg, 1, GFP_KERNEL);
	if (ret)
		goto out;

	sg_init_table(sg->sgl, 1);
	sg_dma_len(sg->sgl) = obj->size;
	sg_set_page(sg->sgl, pfn_to_page(PFN_DOWN(paddr)), obj->size, 0);
	sg_dma_address(sg->sgl) = paddr;

out:
	if (ret)
		return ERR_PTR(ret);
	return sg;
}

void omap_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
		struct sg_table *sg)
{
	struct drm_gem_object *obj = attachment->dmabuf->priv;
DBG("**** unmap: %p", obj);
	omap_gem_put_paddr(obj);
	sg_free_table(sg);
	kfree(sg);
}

void omap_gem_dmabuf_release(struct dma_buf *buffer)
{
	struct drm_gem_object *obj = buffer->priv;
DBG("**** release: %p", obj);
if (obj) DBG("**** cnt: %d", obj->refcount.refcount.counter);
	/* release reference that was taken when dmabuf was exported
	 * in omap_gem_prime_set()..
	 */
	drm_gem_object_unreference_unlocked(obj);
}

struct dma_buf_ops omap_dmabuf_ops = {
		.map_dma_buf = omap_gem_map_dma_buf,
		.unmap_dma_buf = omap_gem_unmap_dma_buf,
		.release = omap_gem_dmabuf_release,
};

int omap_gem_prime_set(struct drm_device *dev,
		struct drm_file *file_priv,
		uint32_t handle, int *prime_fd)
{
	struct drm_gem_object *obj;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	obj = drm_gem_object_lookup(dev, file_priv, handle);
DBG("**** export: %p", obj);
	if (!obj) {
		ret = -ENOENT;
		goto unlock;
	}

	/* note: dmabuf takes ownership of the bo ref */
	obj->dma_buf = dma_buf_export(obj, &omap_dmabuf_ops, obj->size, 0600);
	if (obj->dma_buf)
		*prime_fd = dma_buf_fd(obj->dma_buf);
	else
		ret = -ENOMEM;

unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int omap_gem_prime_get(struct drm_device *dev,
		struct drm_file *file_priv,
		int prime_fd, uint32_t *handle)
{
	return -EINVAL;
}
