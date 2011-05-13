/*
 * linux/drivers/gpu/drm/omap/omap_fb.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
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

#include <plat/vram.h>

#include <linux/omap_gpu.h>
#include "omap_gpu_priv.h"

#include "drm_crtc.h"
#include "drm_crtc_helper.h"


static char *def_vram;
module_param_named(vram, def_vram, charp, 0);

/*
 * framebuffer funcs
 */

#define to_omap_framebuffer(x) container_of(x, struct omap_framebuffer, base)

struct omap_framebuffer {
	struct drm_framebuffer base;

	/* framebuffer size/phys-addr/virt-addr */
	int size;
	unsigned long paddr;
	void __iomem *vaddr;
};

static int omap_framebuffer_create_handle(struct drm_framebuffer *fb,
						struct drm_file *file_priv,
						unsigned int *handle)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	DBG("framebuffer: get handle: %p", omap_fb);

	// TODO, I suppose this really should be some sort of GEM handle
	// to the framebuffer object, in case it needs to be mapped or
	// something.  Right now this will go-exist badly with PVR, who
	// implements the mmap() fxn.. need to think about how to handle
	// this..

	*handle = 42;

	return 0;
}

static void omap_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);

	DBG("destroy: FB ID: %d (%p)", fb->base.id, fb);

	drm_framebuffer_cleanup(fb);

	if (omap_fb->vaddr) {
		iounmap(omap_fb->vaddr);
	}

	/* drm framework should, but doesn't (as of 2.6.35) disconnect any
	 * CRTCs connected to this fb before destroying it.. so could be
	 * some small window when garbage is seen on screen.  But in
	 * practice, unlikely because we have a private vram pool.  So I
	 * won't worry too much about it.
	 */
	if (omap_fb->paddr) {
		omap_vram_free(omap_fb->paddr, omap_fb->size);
	}

	kfree(omap_fb);
}

static int omap_framebuffer_dirty(struct drm_framebuffer *fb,
		struct drm_file *file_priv, unsigned flags, unsigned color,
		struct drm_clip_rect *clips, unsigned num_clips)
{
	int i;

	for (i = 0; i < num_clips; i++) {
		omap_framebuffer_flush(fb, clips[i].x1, clips[i].y1,
					clips[i].x2 - clips[i].x1,
					clips[i].y2 - clips[i].y1);
	}

	return 0;
}

static const struct drm_framebuffer_funcs omap_framebuffer_funcs = {
	.create_handle = omap_framebuffer_create_handle,
	.destroy = omap_framebuffer_destroy,
	.dirty = omap_framebuffer_dirty,
};

int omap_framebuffer_get_buffer(struct drm_framebuffer *fb, int x, int y,
		void **vaddr, unsigned long *paddr, int *screen_width)
{
	struct omap_framebuffer *omap_fb = to_omap_framebuffer(fb);
	int bpp = 4; //XXX fb->depth / 8;
	unsigned long offset;

	offset = (x * bpp) + (y * fb->pitch);

	*vaddr = omap_fb->vaddr + offset;
	*paddr = omap_fb->paddr + offset;
	*screen_width = fb->pitch / bpp;

	return omap_fb->size;
}
EXPORT_SYMBOL(omap_framebuffer_get_buffer);

/* iterate thru all the connectors, returning ones that are attached
 * to the same fb..
 */
struct drm_connector * omap_framebuffer_get_next_connector(
		struct drm_framebuffer *fb, struct drm_connector *from)
{
	struct drm_device *dev = fb->dev;
	struct list_head *connector_list = &dev->mode_config.connector_list;
	struct drm_connector *connector = from;

	if (!from) {
		return list_first_entry(connector_list, typeof(*from), head);
	}

	list_for_each_entry_from(connector, connector_list, head) {
		if (connector != from) {
			struct drm_encoder *encoder = connector->encoder;
			struct drm_crtc *crtc = encoder ? encoder->crtc : NULL;
			if (crtc && crtc->fb == fb) {
				return connector;
			}
		}
	}

	return NULL;
}
EXPORT_SYMBOL(omap_framebuffer_get_next_connector);

/* flush an area of the framebuffer (in case of manual update display that
 * is not automatically flushed)
 */
void omap_framebuffer_flush(struct drm_framebuffer *fb,
		int x, int y, int w, int h)
{
	struct drm_connector *connector = NULL;

	VERB("flush: %d,%d %dx%d, fb=%p", x, y, w, h, fb);

	while ((connector = omap_framebuffer_get_next_connector(fb, connector))) {
		/* only consider connectors that are part of a chain */
		if (connector->encoder && connector->encoder->crtc) {
			/* TODO: maybe this should propagate thru the crtc who
			 * could do the coordinate translation..
			 */
			struct drm_crtc *crtc = connector->encoder->crtc;
			int cx = max(0, x - crtc->x);
			int cy = max(0, y - crtc->y);
			int cw = w + (x - crtc->x) - cx;
			int ch = h + (y - crtc->y) - cy;

			omap_connector_flush(connector, cx, cy, cw, ch);
		}
	}
}
EXPORT_SYMBOL(omap_framebuffer_flush);

struct drm_framebuffer * omap_framebuffer_create(struct drm_device *dev,
        struct drm_file *file, struct drm_mode_fb_cmd *mode_cmd)
{
	return omap_framebuffer_init(dev, mode_cmd);
}

struct drm_framebuffer * omap_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd *mode_cmd)
{
	struct omap_framebuffer *omap_fb;
	struct drm_framebuffer *fb = NULL;
	unsigned long paddr;
	int size, ret;

	/* in case someone tries to feed us a completely bogus stride: */
	mode_cmd->pitch = max(mode_cmd->pitch,
			mode_cmd->width * mode_cmd->bpp / 8);

	/* pvr needs to have a stride that is a multiple of 8 pixels: */
	mode_cmd->pitch = ALIGN(mode_cmd->pitch, 8 * (mode_cmd->bpp / 8));

	DBG("create framebuffer: dev=%p, mode_cmd=%p (%dx%d)", dev,
			mode_cmd, mode_cmd->width, mode_cmd->height);

	omap_fb = kzalloc(sizeof(*omap_fb), GFP_KERNEL);
	if (!omap_fb) {
		dev_err(dev->dev, "could not allocate fb\n");
		goto fail;
	}

	fb = &omap_fb->base;
	ret = drm_framebuffer_init(dev, fb, &omap_framebuffer_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		goto fail;
	}

	DBG("create: FB ID: %d (%p)", fb->base.id, fb);

	size = PAGE_ALIGN(mode_cmd->pitch * mode_cmd->height);

	DBG("allocating %d bytes for fb %d", size, dev->primary->index);
	ret = omap_vram_alloc(OMAP_VRAM_MEMTYPE_SDRAM, size, &paddr);
	if (ret) {
		dev_err(dev->dev, "failed to allocate vram\n");
		goto fail;
	}

	omap_fb->size = size;
	omap_fb->paddr = paddr;
	omap_fb->vaddr = ioremap_wc(paddr, size);

	if (!omap_fb->vaddr) {
		dev_err(dev->dev, "failed to ioremap framebuffer\n");
		goto fail;
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	return fb;

fail:
	if (fb) {
		omap_framebuffer_destroy(fb);
	}
	return NULL;
}
EXPORT_SYMBOL(omap_framebuffer_init);
