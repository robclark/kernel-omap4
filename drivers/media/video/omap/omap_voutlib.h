/*
 * omap_voutlib.h
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifndef OMAP_VOUTLIB_H
#define OMAP_VOUTLIB_H

#ifdef CONFIG_TI_TILER
#  include <mach/tiler.h>
#endif

extern void omap_vout_default_crop(struct v4l2_pix_format *pix,
		struct v4l2_framebuffer *fbuf, struct v4l2_rect *crop);

extern int omap_vout_new_crop(struct v4l2_pix_format *pix,
		struct v4l2_rect *crop, struct v4l2_window *win,
		struct v4l2_framebuffer *fbuf,
		const struct v4l2_rect *new_crop);

extern int omap_vout_try_window(struct v4l2_framebuffer *fbuf,
		struct v4l2_window *new_win);

extern int omap_vout_new_window(struct v4l2_rect *crop,
		struct v4l2_window *win, struct v4l2_framebuffer *fbuf,
		struct v4l2_window *new_win);

extern void omap_vout_new_format(struct v4l2_pix_format *pix,
		struct v4l2_framebuffer *fbuf, struct v4l2_rect *crop,
		struct v4l2_window *win);

struct omap_vout_buffer {
	unsigned long size;
	unsigned long vaddr;		/* could be NULL if no kernel mapping */
	unsigned long paddr;
#ifdef CONFIG_TI_TILER
	struct tiler_block_t blk;
#endif
};

int omap_vout_alloc_buffer(struct omap_vout_buffer *buf, u32 buf_size);
void omap_vout_free_buffer(struct omap_vout_buffer *buf);
#endif	/* #ifndef OMAP_VOUTLIB_H */

