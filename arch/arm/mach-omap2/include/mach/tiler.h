/*
 * tiler.h
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

#ifndef TILER_H
#define TILER_H

#define TILER_PAGE 0x1000
#define TILER_WIDTH    256
#define TILER_HEIGHT   128
#define TILER_BLOCK_WIDTH  64
#define TILER_BLOCK_HEIGHT 64
#define TILER_LENGTH (TILER_WIDTH * TILER_HEIGHT * TILER_PAGE)

#define TILER_MAX_NUM_BLOCKS 16

#define TILIOC_GBLK  _IOWR('z', 100, struct tiler_block_info)
#define TILIOC_FBLK   _IOW('z', 101, struct tiler_block_info)
#define TILIOC_GSSP  _IOWR('z', 102, u32)
#define TILIOC_MBLK  _IOWR('z', 103, struct tiler_block_info)
#define TILIOC_UMBLK  _IOW('z', 104, struct tiler_block_info)
#define TILIOC_QBUF  _IOWR('z', 105, struct tiler_buf_info)
#define TILIOC_RBUF  _IOWR('z', 106, struct tiler_buf_info)
#define TILIOC_URBUF  _IOW('z', 107, struct tiler_buf_info)
#define TILIOC_QBLK  _IOWR('z', 108, struct tiler_block_info)

enum tiler_fmt {
	TILFMT_MIN     = -2,
	TILFMT_INVALID = -2,
	TILFMT_NONE    = -1,
	TILFMT_8BIT    = 0,
	TILFMT_16BIT   = 1,
	TILFMT_32BIT   = 2,
	TILFMT_PAGE    = 3,
	TILFMT_MAX     = 3,
};

struct tiler_block_t {
	u32 phys;			/* system space (L3) tiler addr */
	u32 width;			/* width */
	u32 height;			/* height */
	u32 key;			/* secret key */
	u32 id;				/* unique block ID */
};

struct area {
	u16 width;
	u16 height;
};

struct tiler_block_info {
	enum tiler_fmt fmt;
	union {
		struct area area;
		u32 len;
	} dim;
	u32 stride;
	void *ptr;
	u32 id;
	u32 key;
	u32 group_id;
	/* alignment requirements for ssptr: ssptr & (align - 1) == offs */
	u32 align;
	u32 offs;
	u32 ssptr;
};

struct tiler_buf_info {
	u32 num_blocks;
	struct tiler_block_info blocks[TILER_MAX_NUM_BLOCKS];
	u32 offset;
	u32 length;	/* also used as number of buffers for reservation */
};

struct tiler_view_orient {
	u8 rotate_90;
	u8 x_invert;
	u8 y_invert;
};

/**
 * Reserves a 1D or 2D TILER block area and memory for the
 * current process with group ID 0.
 *
 * @param blk	pointer to tiler block data.  This must be set up ('phys' member
 *		must be 0) with the tiler block information. 'height' must be 1
 *		for 1D block.
 * @param fmt	TILER block format
 * @param align	block alignment (default: PAGE_SIZE)
 * @param offs	block offset
 *
 * @return error status
 */
s32 tiler_alloc(struct tiler_block_t *blk, enum tiler_fmt fmt, u32 align,
		u32 offs);

/**
 * Reserves a 1D or 2D TILER block area and memory for a set process and group
 * ID.
 *
 * @param blk	pointer to tiler block data.  This must be set up ('phys' member
 *		must be 0) with the tiler block information. 'height' must be 1
 *		for 1D block.
 * @param fmt	TILER bit mode
 * @param align	block alignment (default: PAGE_SIZE)
 * @param offs	block offset
 * @param gid	group ID
 * @param pid	process ID
 *
 * @return error status
 */
s32 tiler_allocx(struct tiler_block_t *blk, enum tiler_fmt fmt, u32 align,
		u32 offs, u32 gid, pid_t pid);

/**
 * Maps an existing buffer to a 1D or 2D TILER area for the
 * current process with group ID 0.
 *
 * Currently, only 1D area mapping is supported.
 *
 * NOTE: alignment is always PAGE_SIZE and offset is 0 as full pages are mapped
 * into tiler container.
 *
 * @param blk		pointer to tiler block data.  This must be set up
 *		('phys' member must be 0) with the tiler block information.
 *		'height' must be 1 for 1D block.
 * @param fmt		TILER bit mode
 * @param usr_addr	user space address of existing buffer.
 *
 * @return error status
 */
s32 tiler_map(struct tiler_block_t *blk, enum tiler_fmt fmt, u32 usr_addr);

/**
 * Maps an existing buffer to a 1D or 2D TILER area for a set process and group
 * ID.
 *
 * Currently, only 1D area mapping is supported.
 *
 * NOTE: alignment is always PAGE_SIZE and offset is 0 as full pages are mapped
 * into tiler container.
 *
 * @param blk		pointer to tiler block data.  This must be set up
 *		('phys' member must be 0) with the tiler block information.
 *		'height' must be 1 for 1D block.
 * @param fmt		TILER bit mode
 * @param gid		group ID
 * @param pid		process ID
 * @param usr_addr	user space address of existing buffer.
 *
 * @return error status
 */
s32 tiler_mapx(struct tiler_block_t *blk, enum tiler_fmt fmt,
		u32 gid, pid_t pid, u32 usr_addr);

/**
 * Frees TILER memory.  Since there may be multiple references for the same area
 * if duplicated by tiler_dup, the area is only actually freed if all references
 * have been freed.
 *
 * @param blk	pointer to a tiler block data as filled by tiler_alloc,
 *		tiler_map or tiler_dup.  'phys' member will be set to 0 on
 *		success.
 *
 * @return an error status.
 */
s32 tiler_free(struct tiler_block_t *blk);

/**
 * Reserves tiler area for n identical set of blocks (buffer)
 * for the current process.  Use this method to get optimal
 * placement of multiple related tiler blocks; however, it may
 * not reserve area if tiler_alloc is equally efficient.
 *
 * @param n	number of identical set of blocks
 * @param b	information on the set of blocks (ptr, ssptr and
 *		stride fields are ignored)
 *
 * @return error status
 */
s32 tiler_reserve(u32 n, struct tiler_buf_info *b);

/**
 * Reserves tiler area for n identical set of blocks (buffer) fo
 * a given process. Use this method to get optimal placement of
 * multiple related tiler blocks; however, it may not reserve
 * area if tiler_alloc is equally efficient.
 *
 * @param n	number of identical set of blocks
 * @param b	information on the set of blocks (ptr, ssptr and
 *		stride fields are ignored)
 * @param pid   process ID
 *
 * @return error status
 */
s32 tiler_reservex(u32 n, struct tiler_buf_info *b, pid_t pid);

u32 tiler_reorient_addr(u32 tsptr, struct tiler_view_orient orient);

u32 tiler_get_natural_addr(void *sys_ptr);

u32 tiler_reorient_topleft(u32 tsptr, struct tiler_view_orient orient,
				u32 width, u32 height);

u32 tiler_stride(u32 tsptr);

void tiler_rotate_view(struct tiler_view_orient *orient, u32 rotation);

#endif

