/*
 * tiler_rot.c
 *
 * TILER driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2009-2010 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <mach/tiler.h>
#include "tiler-def.h"
#include <mach/dmm.h> /* TEMP */
#include "tcm.h"
#include "_tiler.h"

#define SLOT_WIDTH_BITS		6
#define SLOT_HEIGHT_BITS	6

static struct tiler_geom geom[TILER_FORMATS] = {
	{
		.x_shft = 0,
		.y_shft = 0,
	},
	{
		.x_shft = 0,
		.y_shft = 1,
	},
	{
		.x_shft = 1,
		.y_shft = 1,
	},
	{
		.x_shft = SLOT_WIDTH_BITS,
		.y_shft = SLOT_HEIGHT_BITS,
	},
};

#define DMM_ROTATION_SHIFT	31
#define DMM_Y_INVERT_SHIFT	30
#define DMM_X_INVERT_SHIFT	29
#define DMM_ACC_MODE_SHIFT	27
#define DMM_ACC_MODE_MASK	3
#define CONT_WIDTH_BITS		14
#define CONT_HEIGHT_BITS	13

#define TILER_PAGE		(1 << (SLOT_WIDTH_BITS + SLOT_HEIGHT_BITS))
#define TILER_WIDTH		(1 << (CONT_WIDTH_BITS - SLOT_WIDTH_BITS))
#define TILER_HEIGHT		(1 << (CONT_HEIGHT_BITS - SLOT_HEIGHT_BITS))

#define VIEW_SIZE		(1u << (CONT_WIDTH_BITS + CONT_HEIGHT_BITS))
#define VIEW_MASK		(VIEW_SIZE - 1u)

#define MASK(bits) ((1 << (bits)) - 1)

#define TILER_GET_ACC_MODE(x)	((enum tiler_fmt) \
		((x >> DMM_ACC_MODE_SHIFT) & DMM_ACC_MODE_MASK))
#define DMM_IS_X_INVERTED(x)	((x >> DMM_X_INVERT_SHIFT) & 1)
#define DMM_IS_Y_INVERTED(x)	((x >> DMM_Y_INVERT_SHIFT) & 1)
#define DMM_IS_ROTATED(x)	((x >> DMM_ROTATION_SHIFT) & 1)

#define DMM_ALIAS_VIEW_CLEAR	(~((1 << DMM_X_INVERT_SHIFT) | \
		(1 << DMM_Y_INVERT_SHIFT) | (1 << DMM_ROTATION_SHIFT)))

#define TILVIEW_8BIT	TILER_ALIAS_BASE
#define TILVIEW_16BIT	(TILVIEW_8BIT  + VIEW_SIZE)
#define TILVIEW_32BIT	(TILVIEW_16BIT + VIEW_SIZE)
#define TILVIEW_PAGE	(TILVIEW_32BIT + VIEW_SIZE)
#define TILVIEW_END	(TILVIEW_PAGE  + VIEW_SIZE)

#define TIL_ADDR(x, r, yi, xi, a)\
	((u32) x | (((r) ? 1 : 0) << DMM_ROTATION_SHIFT) | \
	(((yi) ? 1 : 0) << DMM_Y_INVERT_SHIFT) | \
	(((xi) ? 1 : 0) << DMM_X_INVERT_SHIFT) | ((a) << DMM_ACC_MODE_SHIFT))

/*
 * TILER Memory model query method
 */
bool is_tiler_addr(u32 phys)
{
	return phys >= TILVIEW_8BIT && phys < TILVIEW_END;
}

/*
 * TILER block query method
 */

u32 tiler_bpp(const struct tiler_block_t *b)
{
	enum tiler_fmt fmt = tiler_fmt(b->phys);
	BUG_ON(fmt == TILFMT_INVALID);

	/* return modified bpp */
	return geom[fmt].bpp_m;
}
EXPORT_SYMBOL(tiler_bpp);

static inline u32 tiler_stride(enum tiler_fmt fmt, bool rotated)
{
	if (fmt == TILFMT_PAGE)
		return 0;

	return rotated ?
		1 << (CONT_HEIGHT_BITS + geom[fmt].x_shft) :
		1 << (CONT_WIDTH_BITS + geom[fmt].y_shft);
}

u32 tiler_pstride(const struct tiler_block_t *b)
{
	enum tiler_fmt fmt = tiler_fmt(b->phys);
	BUG_ON(fmt == TILFMT_INVALID);

	/* return the virtual stride for page mode */
	if (fmt == TILFMT_PAGE)
		return tiler_vstride(b);

	return tiler_stride(fmt, 0);
}
EXPORT_SYMBOL(tiler_pstride);

enum tiler_fmt tiler_fmt(u32 phys)
{
	if (!is_tiler_addr(phys))
		return TILFMT_INVALID;

	return TILER_GET_ACC_MODE(phys);
}
EXPORT_SYMBOL(tiler_fmt);

static const struct tiler_geom *get_geom(enum tiler_fmt fmt)
{
	if (fmt >= TILFMT_MIN && fmt <= TILFMT_MAX)
		return geom + fmt;
	return NULL;
}

static void tiler_get_natural_xy(u32 tsptr, u32 *x, u32 *y)
{
	u32 x_bits, y_bits, offset;
	enum tiler_fmt fmt;

	fmt = TILER_GET_ACC_MODE(tsptr);

	x_bits = CONT_WIDTH_BITS - geom[fmt].x_shft;
	y_bits = CONT_HEIGHT_BITS - geom[fmt].y_shft;
	offset = (tsptr & VIEW_MASK) >> (geom[fmt].x_shft + geom[fmt].y_shft);

	if (DMM_IS_ROTATED(tsptr)) {
		*x = offset >> y_bits;
		*y = offset & MASK(y_bits);
	} else {
		*x = offset & MASK(x_bits);
		*y = offset >> x_bits;
	}

	if (DMM_IS_X_INVERTED(tsptr))
		*x ^= MASK(x_bits);
	if (DMM_IS_Y_INVERTED(tsptr))
		*y ^= MASK(y_bits);
}

static u32 tiler_get_address(struct tiler_view_orient orient,
			enum tiler_fmt fmt, u32 x, u32 y)
{
	u32 x_bits, y_bits, tmp, x_mask, y_mask, alignment;

	x_bits = CONT_WIDTH_BITS - geom[fmt].x_shft;
	y_bits = CONT_HEIGHT_BITS - geom[fmt].y_shft;
	alignment = geom[fmt].x_shft + geom[fmt].y_shft;

	x_mask = MASK(x_bits);
	y_mask = MASK(y_bits);
	if (x < 0 || x > x_mask || y < 0 || y > y_mask)
		return 0;

	if (orient.x_invert)
		x ^= x_mask;
	if (orient.y_invert)
		y ^= y_mask;

	if (orient.rotate_90)
		tmp = ((x << y_bits) + y);
	else
		tmp = ((y << x_bits) + x);

	return TIL_ADDR((tmp << alignment), orient.rotate_90,
			orient.y_invert, orient.x_invert, fmt);
}

u32 tiler_reorient_addr(u32 tsptr, struct tiler_view_orient orient)
{
	u32 x, y;

	tiler_get_natural_xy(tsptr, &x, &y);
	return tiler_get_address(orient, TILER_GET_ACC_MODE(tsptr), x, y);
}
EXPORT_SYMBOL(tiler_reorient_addr);

u32 tiler_get_natural_addr(void *sys_ptr)
{
	return (u32)sys_ptr & DMM_ALIAS_VIEW_CLEAR;
}
EXPORT_SYMBOL(tiler_get_natural_addr);

u32 tiler_topleft(u32 tsptr, u32 width, u32 height)
{
	u32 x, y;
	enum tiler_fmt fmt = TILER_GET_ACC_MODE(tsptr);
	struct tiler_view_orient orient;
	orient.x_invert = DMM_IS_X_INVERTED(tsptr);
	orient.y_invert = DMM_IS_Y_INVERTED(tsptr);
	orient.rotate_90 = DMM_IS_ROTATED(tsptr);

	tiler_get_natural_xy(tsptr, &x, &y);

	if (DMM_IS_X_INVERTED(tsptr))
		x += width - 1;
	if (DMM_IS_Y_INVERTED(tsptr))
		y += height - 1;


	return tiler_get_address(orient, fmt, x, y);
}
EXPORT_SYMBOL(tiler_topleft);

u32 tiler_offset_addr(u32 tsptr, u32 x, u32 y)
{
	enum tiler_fmt fmt = TILER_GET_ACC_MODE(tsptr);
	bool rotated = DMM_IS_ROTATED(tsptr);

	return tsptr + x * geom[fmt].bpp_m + y * tiler_stride(fmt, rotated);
}
EXPORT_SYMBOL(tiler_offset_addr);

void tiler_rotate_view(struct tiler_view_orient *orient, u32 rotation)
{
	rotation = (rotation / 90) & 3;

	if (rotation & 2) {
		orient->x_invert = !orient->x_invert;
		orient->y_invert = !orient->y_invert;
	}

	if (rotation & 1) {
		if (orient->rotate_90)
			orient->y_invert = !orient->y_invert;
		else
			orient->x_invert = !orient->x_invert;
		orient->rotate_90 = !orient->rotate_90;
	}
}
EXPORT_SYMBOL(tiler_rotate_view);

void tiler_geom_init(struct tiler_ops *tiler)
{
	struct tiler_geom *g;

	tiler->xy = tiler_get_natural_xy;
	tiler->addr = tiler_get_address;
	tiler->geom = get_geom;

	tiler->page   = TILER_PAGE;
	tiler->width  = TILER_WIDTH;
	tiler->height = TILER_HEIGHT;

	/* calculate geometry */
	for (g = geom; g < geom + TILER_FORMATS; g++) {
		g->bpp_m = g->bpp = 1 << (g->x_shft + g->y_shft);
		g->slot_w = 1 << (SLOT_WIDTH_BITS - g->x_shft);
		g->slot_h = 1 << (SLOT_HEIGHT_BITS - g->y_shft);
	}

	/* set bpp_m = 1 for page mode as most applications deal in byte data */
	geom[TILFMT_PAGE].bpp_m = 1;
}
