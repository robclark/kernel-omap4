/*
 * tiler_def.h
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

#ifndef TILER_DEF_H
#define TILER_DEF_H

#define ROUND_UP_2P(a, b) (((a) + (b) - 1) & ~((b) - 1))
#define DIVIDE_UP(a, b) (((a) + (b) - 1) / (b))
#define ROUND_UP(a, b) (DIVIDE_UP(a, b) * (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define TILER_ACC_MODE_SHIFT  (27)
#define TILER_ACC_MODE_MASK   (3)
#define TILER_GET_ACC_MODE(x) ((enum tiler_fmt) (TILFMT_8BIT + \
(((u32)x & (TILER_ACC_MODE_MASK<<TILER_ACC_MODE_SHIFT))>>TILER_ACC_MODE_SHIFT)))

#define TILER_FORMATS 4

#define TILER_ALIAS_BASE    0x60000000u
#define TILVIEW_8BIT	TILER_ALIAS_BASE
#define TILVIEW_16BIT	(TILVIEW_8BIT  + 0x08000000u)
#define TILVIEW_32BIT	(TILVIEW_16BIT + 0x08000000u)
#define TILVIEW_PAGE	(TILVIEW_32BIT + 0x08000000u)
#define TILVIEW_END	(TILVIEW_PAGE  + 0x08000000u)

#define TIL_ADDR(x, r, yi, xi, a)\
((void *)((u32)x | (r << DMM_ROTATION_SHIFT) |\
(yi << DMM_Y_INVERT_SHIFT) | (xi << DMM_X_INVERT_SHIFT) |\
(a << TILER_ACC_MODE_SHIFT)))

#define DMM_X_INVERT_SHIFT	(29)
#define DMM_IS_X_INVERTED(x)	((x >> DMM_X_INVERT_SHIFT) & 1)
#define DMM_Y_INVERT_SHIFT	(30)
#define DMM_IS_Y_INVERTED(x)	((x >> DMM_Y_INVERT_SHIFT) & 1)
#define DMM_ROTATION_SHIFT	(31)
#define DMM_IS_ROTATED(x)	((x >> DMM_ROTATION_SHIFT) & 1)

#define DMM_ALIAS_VIEW_CLEAR    (~0xE0000000)

#define DMM_TILE_DIMM_X_MODE_8    (32)
#define DMM_TILE_DIMM_Y_MODE_8    (32)

#define DMM_TILE_DIMM_X_MODE_16    (32)
#define DMM_TILE_DIMM_Y_MODE_16    (16)

#define DMM_TILE_DIMM_X_MODE_32    (16)
#define DMM_TILE_DIMM_Y_MODE_32    (16)

#define DMM_PAGE_DIMM_X_MODE_8    (DMM_TILE_DIMM_X_MODE_8*2)
#define DMM_PAGE_DIMM_Y_MODE_8    (DMM_TILE_DIMM_Y_MODE_8*2)

#define DMM_PAGE_DIMM_X_MODE_16    (DMM_TILE_DIMM_X_MODE_16*2)
#define DMM_PAGE_DIMM_Y_MODE_16    (DMM_TILE_DIMM_Y_MODE_16*2)

#define DMM_PAGE_DIMM_X_MODE_32    (DMM_TILE_DIMM_X_MODE_32*2)
#define DMM_PAGE_DIMM_Y_MODE_32    (DMM_TILE_DIMM_Y_MODE_32*2)

#endif
