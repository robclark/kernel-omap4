/*
 * tiler.c
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
#include <linux/cdev.h>            /* struct cdev */
#include <linux/kdev_t.h>          /* MKDEV() */
#include <linux/fs.h>              /* register_chrdev_region() */
#include <linux/device.h>          /* struct class */
#include <linux/platform_device.h> /* platform_device() */
#include <linux/err.h>             /* IS_ERR() */
#include <linux/uaccess.h>         /* copy_to_user */
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>         /* page_cache_release() */
#include <linux/slab.h>

#include <asm/mach/map.h>              /* for ioremap_page */
#include <mach/tiler.h>
#include <mach/dmm.h>
#include "tmm.h"
#include "tiler-def.h"
#include "tcm/tcm-sita.h"	/* Algo Specific header */
#include "tiler-reserve.h"

#include <linux/syscalls.h>

static bool security = CONFIG_TILER_SECURITY;
static bool ssptr_id = CONFIG_TILER_SSPTR_ID;
static bool ssptr_lookup = true;
static bool offset_lookup = true;
static uint default_align = CONFIG_TILER_ALIGNMENT;
static uint granularity = CONFIG_TILER_GRANULARITY;

module_param(ssptr_id, bool, 0644);
MODULE_PARM_DESC(ssptr_id, "Use ssptr as block ID");
module_param_named(align, default_align, uint, 0644);
MODULE_PARM_DESC(align, "Default block ssptr alignment");
module_param_named(grain, granularity, uint, 0644);
MODULE_PARM_DESC(grain, "Granularity (bytes)");
module_param(ssptr_lookup, bool, 0644);
MODULE_PARM_DESC(ssptr_lookup,
	"Allow looking up a block by ssptr - This is a security risk");
module_param(offset_lookup, bool, 0644);
MODULE_PARM_DESC(offset_lookup,
	"Allow looking up a buffer by offset - This is a security risk");
module_param(security, bool, 0644);
MODULE_PARM_DESC(security,
	"Separate allocations by different processes into different pages");

struct tiler_dev {
	struct cdev cdev;
};

struct platform_driver tiler_driver_ldm = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "tiler",
	},
	.probe = NULL,
	.shutdown = NULL,
	.remove = NULL,
};

/* per process (thread group) info */
struct process_info {
	struct list_head list;		/* other processes */
	struct list_head groups;	/* my groups */
	struct list_head bufs;		/* my registered buffers */
	pid_t pid;			/* really: thread group ID */
	u32 refs;			/* open tiler devices, 0 for processes
					   tracked via kernel APIs */
	bool kernel;			/* tracking kernel objects */
};

/* per group info (within a process) */
struct gid_info {
	struct list_head by_pid;	/* other groups */
	struct list_head areas;		/* all areas in this pid/gid */
	struct list_head reserved;	/* areas pre-reserved */
	struct list_head onedim;	/* all 1D areas in this pid/gid */
	u32 gid;			/* group ID */
	u32 refs;			/* instances directly using this ptr */
	struct process_info *pi;	/* parent */
};

struct list_head blocks;
struct list_head procs;
struct list_head orphan_areas;
struct list_head orphan_onedim;

struct area_info {
	struct list_head by_gid;	/* areas in this pid/gid */
	struct list_head blocks;	/* blocks in this area */
	u32 nblocks;			/* # of blocks in this area */

	struct tcm_area area;		/* area details */
	struct gid_info *gi;		/* link to parent, if still alive */
};

struct mem_info {
	struct list_head global;	/* reserved / global blocks */
	struct tiler_block_t blk;	/* block info */
	u32 num_pg;			/* number of pages in page-list */
	u32 usr;			/* user space address */
	u32 *pg_ptr;			/* list of mapped struct page ptrs */
	struct tcm_area area;
	u32 *mem;			/* list of alloced phys addresses */
	u32 refs;			/* number of times referenced */
	bool alloced;			/* still alloced */

	struct list_head by_area;	/* blocks in the same area / 1D */
	void *parent;			/* area info for 2D, else group info */
};

struct __buf_info {
	struct list_head by_pid;		/* list of buffers per pid */
	struct tiler_buf_info buf_info;
	struct mem_info *mi[TILER_MAX_NUM_BLOCKS];	/* blocks */
};

#define TILER_FORMATS 4

static s32 tiler_major;
static s32 tiler_minor;
static struct tiler_dev *tiler_device;
static struct class *tilerdev_class;
static struct mutex mtx;
static struct tcm *tcm[TILER_FORMATS];
static struct tmm *tmm[TILER_FORMATS];

#define TCM(fmt)        tcm[(fmt) - TILFMT_8BIT]
#define TCM_SS(ssptr)   TCM(TILER_GET_ACC_MODE(ssptr))
#define TCM_SET(fmt, i) tcm[(fmt) - TILFMT_8BIT] = i
#define TMM(fmt)        tmm[(fmt) - TILFMT_8BIT]
#define TMM_SS(ssptr)   TMM(TILER_GET_ACC_MODE(ssptr))
#define TMM_SET(fmt, i) tmm[(fmt) - TILFMT_8BIT] = i

static const u32 tiler_bpps[4]    = { 1, 2, 4, 1 };
static const u32 tiler_strides[4] = { 16384, 32768, 32768, 0 };

static u32 is_tiler_addr(u32 addr)
{
	return addr >= TILVIEW_8BIT && addr < TILVIEW_END;
}

/* get tiler format */
static inline u32 tiler_fmt(const struct tiler_block_t *b)
{
	if (!is_tiler_addr(b->phys))
		BUG();
	/* return TILER_GET_ACC_MODE(b->phys); */
	return TILFMT_8BIT + (((b->phys - TILVIEW_8BIT) >> 27) & 3);
}

/* get tiler block bpp */
static inline u32 tiler_bpp(const struct tiler_block_t *b)
{
	return tiler_bpps[tiler_fmt(b) - TILFMT_8BIT];
}

/* get tiler block virtual stride */
static inline u32 tiler_vstride(const struct tiler_block_t *b)
{
	return PAGE_ALIGN((b->phys & ~PAGE_MASK) + tiler_bpp(b) * b->width);
}

/* get tiler block physical stride */
static inline u32 tiler_pstride(const struct tiler_block_t *b)
{
	return tiler_strides[tiler_fmt(b) - TILFMT_8BIT] ? : tiler_vstride(b);
}

/* returns the virtual size of the block (for mmap) */
static inline u32 tiler_size(const struct tiler_block_t *b)
{
	return b->height * tiler_vstride(b);
}

/* get process info, and increment refs for device tracking */
static struct process_info *__get_pi(pid_t pid, bool kernel)
{
	struct process_info *pi;

	/* treat all processes as the same, kernel processes are still treated
	   differently so not to free kernel allocated areas when a user process
	   closes the tiler driver */
	if (!security)
		pid = 0;

	/* find process context */
	mutex_lock(&mtx);
	list_for_each_entry(pi, &procs, list) {
		if (pi->pid == pid && pi->kernel == kernel)
			goto done;
	}

	/* create process context */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi)
		goto done;

	memset(pi, 0, sizeof(*pi));
	pi->pid = pid;
	pi->kernel = kernel;
	INIT_LIST_HEAD(&pi->groups);
	INIT_LIST_HEAD(&pi->bufs);
	list_add(&pi->list, &procs);
done:
	if (pi && !kernel)
		pi->refs++;
	mutex_unlock(&mtx);
	return pi;
}

/* allocate an reserved area of size, alignment and link it to gi */
/* leaves mutex locked to be able to add block to area */
static struct area_info *area_new_m(u16 width, u16 height, u16 align,
				  struct tcm *tcm, struct gid_info *gi)
{
	struct area_info *ai = kmalloc(sizeof(*ai), GFP_KERNEL);
	if (!ai)
		return NULL;

	/* set up empty area info */
	memset(ai, 0x0, sizeof(*ai));
	INIT_LIST_HEAD(&ai->blocks);

	/* reserve an allocation area */
	if (tcm_reserve_2d(tcm, width, height, align, &ai->area)) {
		kfree(ai);
		return NULL;
	}

	ai->gi = gi;
	mutex_lock(&mtx);
	list_add_tail(&ai->by_gid, &gi->areas);
	return ai;
}

/* (must have mutex) free an area and return NULL */
static inline void _m_area_free(struct area_info *ai)
{
	if (ai) {
		list_del(&ai->by_gid);
		kfree(ai);
	}
}

static s32 __analize_area(enum tiler_fmt fmt, u32 width, u32 height,
			  u16 *x_area, u16 *y_area, u16 *band,
			  u16 *align, u16 *offs, u16 *in_offs)
{
	/* input: width, height is in pixels, align, offs in bytes */
	/* output: x_area, y_area, band, align, offs in slots */

	/* slot width, height, and row size */
	u32 slot_w, slot_h, slot_row, bpp, min_align;

	/* align must be 2 power */
	if (*align & (*align - 1))
		return -1;

	switch (fmt) {
	case TILFMT_8BIT:
		slot_w = DMM_PAGE_DIMM_X_MODE_8;
		slot_h = DMM_PAGE_DIMM_Y_MODE_8;
		break;
	case TILFMT_16BIT:
		slot_w = DMM_PAGE_DIMM_X_MODE_16;
		slot_h = DMM_PAGE_DIMM_Y_MODE_16;
		break;
	case TILFMT_32BIT:
		slot_w = DMM_PAGE_DIMM_X_MODE_32;
		slot_h = DMM_PAGE_DIMM_Y_MODE_32;
		break;
	case TILFMT_PAGE:
		/* adjust size to accomodate offset, only do page alignment */
		*align = PAGE_SIZE;
		width += *offs & (PAGE_SIZE - 1);

		/* for 1D area keep the height (1), width is in tiler slots */
		*x_area = DIV_ROUND_UP(width, TILER_PAGE);
		*y_area = *band = 1;

		if (*x_area * *y_area > TILER_WIDTH * TILER_HEIGHT)
			return -1;
		return 0;
	default:
		return -EINVAL;
	}

	/* get the # of bytes per row in 1 slot */
	bpp = tiler_bpps[fmt - TILFMT_8BIT];
	slot_row = slot_w * bpp;

	/* how many slots are can be accessed via one physical page */
	*band = PAGE_SIZE / slot_row;

	/* minimum alignment is at least 1 slot.  Use default if needed */
	min_align = MAX(slot_row, granularity);
	*align = ALIGN(*align ? : default_align, min_align);

	/* offset must be multiple of bpp */
	if (*offs & (bpp - 1))
		return -EINVAL;

	/* round down the offset to the nearest slot size, and increase width
	   to allow space for having the correct offset */
	width += (*offs & (min_align - 1)) / bpp;
	if (in_offs)
		*in_offs = *offs & (min_align - 1);
	*offs &= ~(min_align - 1);

	/* expand width to block size */
	width = ALIGN(width, min_align / bpp);

	/* adjust to slots */
	*x_area = DIV_ROUND_UP(width, slot_w);
	*y_area = DIV_ROUND_UP(height, slot_h);
	*align /= slot_row;
	*offs /= slot_row;

	if (*x_area > TILER_WIDTH || *y_area > TILER_HEIGHT)
		return -1;
	return 0x0;
}

/**
 * Find a place where a 2D block would fit into a 2D area of the
 * same height.
 *
 * @author a0194118 (3/19/2010)
 *
 * @param w	Width of the block.
 * @param align	Alignment of the block.
 * @param offs	Offset of the block (within alignment)
 * @param ai	Pointer to area info
 * @param next	Pointer to the variable where the next block
 *		will be stored.  The block should be inserted
 *		before this block.
 *
 * @return the end coordinate (x1 + 1) where a block would fit,
 *	   or 0 if it does not fit.
 *
 * (must have mutex)
 */
static u16 _m_blk_find_fit(u16 w, u16 align, u16 offs,
		     struct area_info *ai, struct list_head **before)
{
	int x = ai->area.p0.x + w + offs;
	struct mem_info *mi;

	/* area blocks are sorted by x */
	list_for_each_entry(mi, &ai->blocks, by_area) {
		/* check if buffer would fit before this area */
		if (x <= mi->area.p0.x) {
			*before = &mi->by_area;
			return x;
		}
		x = ALIGN(mi->area.p1.x + 1 - offs, align) + w + offs;
	}
	*before = &ai->blocks;

	/* check if buffer would fit after last area */
	return (x <= ai->area.p1.x + 1) ? x : 0;
}

/* (must have mutex) adds a block to an area with certain x coordinates */
static inline
struct mem_info *_m_add2area(struct mem_info *mi, struct area_info *ai,
				u16 x0, u16 w, struct list_head *before)
{
	mi->parent = ai;
	mi->area = ai->area;
	mi->area.p0.x = x0;
	mi->area.p1.x = x0 + w - 1;
	list_add_tail(&mi->by_area, before);
	ai->nblocks++;
	return mi;
}

static struct mem_info *get_2d_area(u16 w, u16 h, u16 align, u16 offs, u16 band,
					struct gid_info *gi, struct tcm *tcm) {
	struct area_info *ai = NULL;
	struct mem_info *mi = NULL;
	struct list_head *before = NULL;
	u16 x = 0;   /* this holds the end of a potential area */

	/* allocate map info */

	/* see if there is available prereserved space */
	mutex_lock(&mtx);
	list_for_each_entry(mi, &gi->reserved, global) {
		if (mi->area.tcm == tcm &&
		    tcm_aheight(mi->area) == h &&
		    tcm_awidth(mi->area) == w &&
		    (mi->area.p0.x & (align - 1)) == offs) {
			/* this area is already set up */

			/* remove from reserved list */
			list_del(&mi->global);
			goto done;
		}
	}
	mutex_unlock(&mtx);

	/* if not, reserve a block struct */
	mi = kmalloc(sizeof(*mi), GFP_KERNEL);
	if (!mi)
		return mi;
	memset(mi, 0, sizeof(*mi));

	/* see if allocation fits in one of the existing areas */
	/* this sets x, ai and before */
	mutex_lock(&mtx);
	list_for_each_entry(ai, &gi->areas, by_gid) {
		if (ai->area.tcm == tcm &&
		    tcm_aheight(ai->area) == h) {
			x = _m_blk_find_fit(w, align, offs, ai, &before);
			if (x) {
				_m_add2area(mi, ai, x - w, w, before);
				goto done;
			}
		}
	}
	mutex_unlock(&mtx);

	/* if no area fit, reserve a new one */
	ai = area_new_m(ALIGN(w + offs, max(band, align)), h,
		      max(band, align), tcm, gi);
	if (ai) {
		_m_add2area(mi, ai, ai->area.p0.x + offs, w, &ai->blocks);
	} else {
		/* clean up */
		kfree(mi);
		return NULL;
	}

done:
	mutex_unlock(&mtx);
	return mi;
}

/* (must have mutex) */
static void _m_try_free_group(struct gid_info *gi)
{
	if (gi && list_empty(&gi->areas) && list_empty(&gi->onedim) &&
	    /* also ensure noone is still using this group */
	    !gi->refs) {
		BUG_ON(!list_empty(&gi->reserved));
		list_del(&gi->by_pid);

		/* if group is tracking kernel objects, we may free even
		   the process info */
		if (gi->pi->kernel && list_empty(&gi->pi->groups)) {
			list_del(&gi->pi->list);
			kfree(gi->pi);
		}

		kfree(gi);
	}
}

static void clear_pat(struct tmm *tmm, struct tcm_area *area)
{
	struct pat_area p_area = {0};
	struct tcm_area slice, area_s;

	tcm_for_each_slice(slice, *area, area_s) {
		p_area.x0 = slice.p0.x;
		p_area.y0 = slice.p0.y;
		p_area.x1 = slice.p1.x;
		p_area.y1 = slice.p1.y;

		tmm_clear(tmm, p_area);
	}
}

/* (must have mutex) free block and any freed areas */
static s32 _m_free(struct mem_info *mi)
{
	struct area_info *ai = NULL;
	struct page *page = NULL;
	s32 res = 0;
	u32 i;

	/* release memory */
	if (mi->pg_ptr) {
		for (i = 0; i < mi->num_pg; i++) {
			page = (struct page *)mi->pg_ptr[i];
			if (page) {
				if (!PageReserved(page))
					SetPageDirty(page);
				page_cache_release(page);
			}
		}
		kfree(mi->pg_ptr);
	} else if (mi->mem) {
		tmm_free(TMM_SS(mi->blk.phys), mi->mem);
		clear_pat(TMM_SS(mi->blk.phys), &mi->area);
	}

	/* safe deletion as list may not have been assigned */
	if (mi->global.next)
		list_del(&mi->global);
	if (mi->by_area.next)
		list_del(&mi->by_area);

	/* remove block from area first if 2D */
	if (mi->area.is2d) {
		ai = mi->parent;

		/* check to see if area needs removing also */
		if (ai && !--ai->nblocks) {
			res = tcm_free(&ai->area);
			list_del(&ai->by_gid);
			/* try to remove parent if it became empty */
			_m_try_free_group(ai->gi);
			kfree(ai);
			ai = NULL;
		}
	} else {
		/* remove 1D area */
		res = tcm_free(&mi->area);
		/* try to remove parent if it became empty */
		_m_try_free_group(mi->parent);
	}

	kfree(mi);
	return res;
}

/* (must have mutex) returns true if block was freed */
static bool _m_chk_ref(struct mem_info *mi)
{
	/* check references */
	if (mi->refs)
		return 0;

	if (_m_free(mi))
		printk(KERN_ERR "error while removing tiler block\n");

	return 1;
}

/* (must have mutex) */
static inline s32 _m_dec_ref(struct mem_info *mi)
{
	if (mi->refs-- <= 1)
		return _m_chk_ref(mi);

	return 0;
}

/* (must have mutex) */
static inline void _m_inc_ref(struct mem_info *mi)
{
	mi->refs++;
}

/* (must have mutex) returns true if block was freed */
static inline bool _m_try_free(struct mem_info *mi)
{
	if (mi->alloced) {
		mi->refs--;
		mi->alloced = false;
	}
	return _m_chk_ref(mi);
}

/* check if an id is used */
static bool _m_id_in_use(u32 id)
{
	struct mem_info *mi;
	list_for_each_entry(mi, &blocks, global)
		if (mi->blk.id == id)
			return 1;
	return 0;
}

/* check if an offset is used */
static bool _m_offs_in_use(u32 offs, u32 length, struct process_info *pi)
{
	struct __buf_info *_b;
	list_for_each_entry(_b, &pi->bufs, by_pid)
		if (_b->buf_info.offset < offs + length &&
		    _b->buf_info.offset + _b->buf_info.length > offs)
			return 1;
	return 0;
}

/* get an id */
static u32 _m_get_id(void)
{
	static u32 id = 0x2d7ae;

	/* ensure noone is using this id */
	while (_m_id_in_use(id)) {
		/* Galois LSFR: 32, 22, 2, 1 */
		id = (id >> 1) ^ (u32)((0 - (id & 1u)) & 0x80200003u);
	}

	return id;
}

/* get an offset */
static u32 _m_get_offs(struct process_info *pi, u32 length)
{
	static u32 offs = 0xda7a;

	/* ensure no-one is using this offset */
	while ((offs << PAGE_SHIFT) + length < length ||
	       _m_offs_in_use(offs << PAGE_SHIFT, length, pi)) {
		/* Galois LSF: 20, 17 */
		offs = (offs >> 1) ^ (u32)((0 - (offs & 1u)) & 0x90000);
	}

	return offs << PAGE_SHIFT;
}

static s32 register_buf(struct __buf_info *_b, struct process_info *pi)
{
	struct mem_info *mi = NULL;
	struct tiler_buf_info *b = &_b->buf_info;
	u32 i, num = b->num_blocks, remain = num;

	/* check validity */
	if (num > TILER_MAX_NUM_BLOCKS)
		return -EINVAL;

	mutex_lock(&mtx);

	/* find each block */
	b->length = 0;
	list_for_each_entry(mi, &blocks, global) {
		for (i = 0; i < num; i++) {
			if (!_b->mi[i] && mi->blk.id == b->blocks[i].id &&
			    mi->blk.key == b->blocks[i].key) {
				_b->mi[i] = mi;
				b->length += tiler_size(&mi->blk);
				/* quit if found all*/
				if (!--remain)
					break;

			}
		}
	}

	/* if found all, register buffer */
	if (!remain) {
		b->offset = _m_get_offs(pi, b->length);

		list_add(&_b->by_pid, &pi->bufs);

		/* using each block */
		for (i = 0; i < num; i++)
			_m_inc_ref(_b->mi[i]);
	}

	mutex_unlock(&mtx);

	return remain ? -EACCES : 0;
}

/* must have mutex */
static void _m_unregister_buf(struct __buf_info *_b)
{
	u32 i;

	/* unregister */
	list_del(&_b->by_pid);

	/* no longer using the blocks */
	for (i = 0; i < _b->buf_info.num_blocks; i++)
		_m_dec_ref(_b->mi[i]);

	kfree(_b);
}

/**
 * Free all info kept by a process:
 *
 * all registered buffers, allocated blocks, and unreferenced
 * blocks.  Any blocks/areas still referenced will move to the
 * orphaned lists to avoid issues if a new process is created
 * with the same pid.
 *
 * (must have mutex)
 */
static void _m_free_process_info(struct process_info *pi)
{
	struct area_info *ai, *ai_;
	struct mem_info *mi, *mi_;
	struct gid_info *gi, *gi_;
	struct __buf_info *_b = NULL, *_b_ = NULL;
	bool ai_autofreed, need2free;

	/* unregister all buffers */
	list_for_each_entry_safe(_b, _b_, &pi->bufs, by_pid)
		_m_unregister_buf(_b);

	BUG_ON(!list_empty(&pi->bufs));

	/* free all allocated blocks, and remove unreferenced ones */
	list_for_each_entry_safe(gi, gi_, &pi->groups, by_pid) {

		/*
		 * Group info structs when they become empty on an _m_try_free.
		 * However, if the group info is already empty, we need to
		 * remove it manually
		 */
		need2free = list_empty(&gi->areas) && list_empty(&gi->onedim);
		list_for_each_entry_safe(ai, ai_, &gi->areas, by_gid) {
			ai_autofreed = true;
			list_for_each_entry_safe(mi, mi_, &ai->blocks, by_area)
				ai_autofreed &= _m_try_free(mi);

			/* save orphaned areas for later removal */
			if (!ai_autofreed) {
				need2free = true;
				ai->gi = NULL;
				list_move(&ai->by_gid, &orphan_areas);
			}
		}

		list_for_each_entry_safe(mi, mi_, &gi->onedim, by_area) {
			if (!_m_try_free(mi)) {
				need2free = true;
				/* save orphaned 1D blocks */
				mi->parent = NULL;
				list_move(&mi->by_area, &orphan_onedim);
			}
		}

		/* if group is still alive reserved list should have been
		   emptied as there should be no reference on those blocks */
		if (need2free) {
			BUG_ON(!list_empty(&gi->onedim));
			BUG_ON(!list_empty(&gi->areas));
			_m_try_free_group(gi);
		}
	}

	BUG_ON(!list_empty(&pi->groups));
	list_del(&pi->list);
	kfree(pi);
}

static s32 get_area(u32 sys_addr, struct tcm_pt *pt)
{
	enum tiler_fmt fmt;

	sys_addr &= TILER_ALIAS_VIEW_CLEAR;
	fmt = TILER_GET_ACC_MODE(sys_addr);

	switch (fmt) {
	case TILFMT_8BIT:
		pt->x = DMM_HOR_X_PAGE_COOR_GET_8(sys_addr);
		pt->y = DMM_HOR_Y_PAGE_COOR_GET_8(sys_addr);
		break;
	case TILFMT_16BIT:
		pt->x = DMM_HOR_X_PAGE_COOR_GET_16(sys_addr);
		pt->y = DMM_HOR_Y_PAGE_COOR_GET_16(sys_addr);
		break;
	case TILFMT_32BIT:
		pt->x = DMM_HOR_X_PAGE_COOR_GET_32(sys_addr);
		pt->y = DMM_HOR_Y_PAGE_COOR_GET_32(sys_addr);
		break;
	case TILFMT_PAGE:
		pt->x = (sys_addr & 0x7FFFFFF) >> 12;
		pt->y = pt->x / TILER_WIDTH;
		pt->x &= (TILER_WIDTH - 1);
		break;
	default:
		return -EFAULT;
	}
	return 0x0;
}

static u32 __get_alias_addr(enum tiler_fmt fmt, u16 x, u16 y)
{
	u32 acc_mode = -1;
	u32 x_shft = -1, y_shft = -1;

	switch (fmt) {
	case TILFMT_8BIT:
		acc_mode = 0; x_shft = 6; y_shft = 20;
		break;
	case TILFMT_16BIT:
		acc_mode = 1; x_shft = 7; y_shft = 20;
		break;
	case TILFMT_32BIT:
		acc_mode = 2; x_shft = 7; y_shft = 20;
		break;
	case TILFMT_PAGE:
		acc_mode = 3; y_shft = 8;
		break;
	default:
		return 0;
		break;
	}

	if (fmt == TILFMT_PAGE)
		return (u32)TIL_ALIAS_ADDR((x | y << y_shft) << 12, acc_mode);
	else
		return (u32)TIL_ALIAS_ADDR(x << x_shft | y << y_shft, acc_mode);
}

/* must have mutex */
static struct gid_info *_m_get_gi(struct process_info *pi, u32 gid)
{
	struct gid_info *gi;

	/* see if group already exist */
	list_for_each_entry(gi, &pi->groups, by_pid) {
		if (gi->gid == gid)
			goto done;
	}

	/* create new group */
	gi = kmalloc(sizeof(*gi), GFP_KERNEL);
	if (!gi)
		return gi;

	memset(gi, 0, sizeof(*gi));
	INIT_LIST_HEAD(&gi->areas);
	INIT_LIST_HEAD(&gi->onedim);
	INIT_LIST_HEAD(&gi->reserved);
	gi->pi = pi;
	gi->gid = gid;
	list_add(&gi->by_pid, &pi->groups);
done:
	/*
	 * Once area is allocated, the group info's ref count will be
	 * decremented as the reference is no longer needed.
	 */
	gi->refs++;
	return gi;
}

static struct mem_info *__get_area(enum tiler_fmt fmt, u32 width, u32 height,
				   u16 align, u16 offs, struct gid_info *gi)
{
	u16 x, y, band, in_offs = 0;
	struct mem_info *mi = NULL;

	/* calculate dimensions, band, offs and alignment in slots */
	if (__analize_area(fmt, width, height, &x, &y, &band, &align, &offs,
			   &in_offs))
		return NULL;

	if (fmt == TILFMT_PAGE)	{
		/* 1D areas don't pack */
		mi = kmalloc(sizeof(*mi), GFP_KERNEL);
		if (!mi)
			return NULL;
		memset(mi, 0x0, sizeof(*mi));

		if (tcm_reserve_1d(TCM(fmt), x * y, &mi->area)) {
			kfree(mi);
			return NULL;
		}

		mutex_lock(&mtx);
		mi->parent = gi;
		list_add(&mi->by_area, &gi->onedim);
	} else {
		mi = get_2d_area(x, y, align, offs, band, gi, TCM(fmt));
		if (!mi)
			return NULL;

		mutex_lock(&mtx);
	}

	list_add(&mi->global, &blocks);
	mi->alloced = true;
	mi->refs++;
	gi->refs--;
	mutex_unlock(&mtx);

	mi->blk.phys = __get_alias_addr(fmt, mi->area.p0.x, mi->area.p0.y)
		+ in_offs;
	return mi;
}

s32 tiler_mmap_blk(struct tiler_block_t *blk, u32 offs, u32 size,
				struct vm_area_struct *vma, u32 voffs)
{
	u32 v, p;
	u32 len;	/* area to map */

	/* don't allow mremap */
	vma->vm_flags |= VM_DONTEXPAND | VM_RESERVED;

	/* mapping must fit into vma */
	if (vma->vm_start > vma->vm_start + voffs ||
	    vma->vm_start + voffs > vma->vm_start + voffs + size ||
	    vma->vm_start + voffs + size > vma->vm_end)
		BUG();

	/* mapping must fit into block */
	if (offs > offs + size ||
	    offs + size > tiler_size(blk))
		BUG();

	v = tiler_vstride(blk);
	p = tiler_pstride(blk);

	/* remap block portion */
	len = v - (offs % v);	/* initial area to map */
	while (size) {
		if (len > size)
			len = size;

		vma->vm_pgoff = (blk->phys + offs) >> PAGE_SHIFT;
		if (remap_pfn_range(vma, vma->vm_start + voffs, vma->vm_pgoff,
				    len, vma->vm_page_prot))
			return -EAGAIN;
		voffs += len;
		offs += len + p - v;
		size -= len;
		len = v;	/* subsequent area to map */
	}
	return 0;
}
EXPORT_SYMBOL(tiler_mmap_blk);

s32 tiler_ioremap_blk(struct tiler_block_t *blk, u32 offs, u32 size,
				u32 addr, u32 mtype)
{
	u32 v, p;
	u32 len;		/* area to map */
	const struct mem_type *type = get_mem_type(mtype);

	/* mapping must fit into address space */
	if (addr > addr + size)
		BUG();

	/* mapping must fit into block */
	if (offs > offs + size ||
	    offs + size > tiler_size(blk))
		BUG();

	v = tiler_vstride(blk);
	p = tiler_pstride(blk);

	/* move offset and address to end */
	offs += blk->phys + size;
	addr += size;

	len = v - (offs % v);	/* initial area to map */
	while (size) {
		while (len && size) {
			if (ioremap_page(addr - size, offs - size, type))
				return -EAGAIN;
			len  -= PAGE_SIZE;
			size -= PAGE_SIZE;
		}

		offs += p - v;
		len = v;	/* subsequent area to map */
	}
	return 0;
}
EXPORT_SYMBOL(tiler_ioremap_blk);

static s32 tiler_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct __buf_info *_b = NULL;
	struct tiler_buf_info *b = NULL;
	u32 i, map_offs, map_size, blk_offs, blk_size, mapped_size;
	struct process_info *pi = filp->private_data;
	u32 offs = vma->vm_pgoff << PAGE_SHIFT;
	u32 size = vma->vm_end - vma->vm_start;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	mutex_lock(&mtx);
	list_for_each_entry(_b, &pi->bufs, by_pid) {
		if (offs >= _b->buf_info.offset &&
		    offs + size <= _b->buf_info.offset + _b->buf_info.length) {
			b = &_b->buf_info;
			break;
		}
	}
	mutex_unlock(&mtx);
	if (!b)
		return -ENXIO;

	/* mmap relevant blocks */
	blk_offs = _b->buf_info.offset;
	mapped_size = 0;
	for (i = 0; i < b->num_blocks; i++, blk_offs += blk_size) {
		blk_size = tiler_size(&_b->mi[i]->blk);
		if (offs >= blk_offs + blk_size || offs + size < blk_offs)
			continue;
		map_offs = max(offs, blk_offs) - blk_offs;
		map_size = min(size - mapped_size, blk_size);
		if (tiler_mmap_blk(&_b->mi[i]->blk, map_offs, map_size, vma,
				   mapped_size))
			return -EAGAIN;
		mapped_size += map_size;
	}
	return 0;
}

static s32 refill_pat(struct tmm *tmm, struct tcm_area *area, u32 *ptr)
{
	s32 res = 0;
	s32 size = tcm_sizeof(*area) * sizeof(*ptr);
	u32 *page;
	dma_addr_t page_pa;
	struct pat_area p_area = {0};
	struct tcm_area slice, area_s;

	/* must be a 16-byte aligned physical address */
	page = dma_alloc_coherent(NULL, size, &page_pa, GFP_ATOMIC);
	if (!page)
		return -ENOMEM;

	tcm_for_each_slice(slice, *area, area_s) {
		p_area.x0 = slice.p0.x;
		p_area.y0 = slice.p0.y;
		p_area.x1 = slice.p1.x;
		p_area.y1 = slice.p1.y;

		memcpy(page, ptr, sizeof(*ptr) * tcm_sizeof(slice));
		ptr += tcm_sizeof(slice);

		if (tmm_map(tmm, p_area, page_pa)) {
			res = -EFAULT;
			break;
		}
	}

	dma_free_coherent(NULL, size, page, page_pa);

	return res;
}

static s32 map_block(enum tiler_fmt fmt, u32 width, u32 height,
		     u32 key, u32 gid, struct process_info *pi,
		     struct mem_info **info, u32 usr_addr)
{
	u32 i = 0, tmp = -1, *mem = NULL;
	u8 write = 0;
	s32 res = -ENOMEM;
	struct mem_info *mi = NULL;
	struct page *page = NULL;
	struct task_struct *curr_task = current;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct gid_info *gi = NULL;

	*info = NULL;

	/* we only support mapping a user buffer in page mode */
	if (fmt != TILFMT_PAGE)
		return -EPERM;

	/* check if mapping is supported by tmm */
	if (!tmm_can_map(TMM(fmt)))
		return -EPERM;

	/* get group context */
	mutex_lock(&mtx);
	gi = _m_get_gi(pi, gid);
	mutex_unlock(&mtx);

	if (!gi)
		return -ENOMEM;

	/* reserve area in tiler container */
	mi = __get_area(fmt, width, height, 0, 0, gi);
	if (!mi) {
		mutex_lock(&mtx);
		gi->refs--;
		_m_try_free_group(gi);
		mutex_unlock(&mtx);
		return -ENOMEM;
	}

	mi->blk.width = width;
	mi->blk.height = height;
	mi->blk.key = key;
	if (ssptr_id) {
		mi->blk.id = mi->blk.phys;
	} else {
		mutex_lock(&mtx);
		mi->blk.id = _m_get_id();
		mutex_unlock(&mtx);
	}

	mi->usr = usr_addr;

	/* allocate pages */
	mi->num_pg = tcm_sizeof(mi->area);

	mem = kmalloc(mi->num_pg * sizeof(*mem), GFP_KERNEL);
	if (!mem)
		goto done;
	memset(mem, 0x0, sizeof(*mem) * mi->num_pg);

	mi->pg_ptr = kmalloc(mi->num_pg * sizeof(*mi->pg_ptr), GFP_KERNEL);
	if (!mi->pg_ptr)
		goto done;
	memset(mi->pg_ptr, 0x0, sizeof(*mi->pg_ptr) * mi->num_pg);

	/*
	 * Important Note: usr_addr is mapped from user
	 * application process to current process - it must lie
	 * completely within the current virtual memory address
	 * space in order to be of use to us here.
	 */
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, mi->usr);
	res = -EFAULT;

	/*
	 * It is observed that under some circumstances, the user
	 * buffer is spread across several vmas, so loop through
	 * and check if the entire user buffer is covered.
	 */
	while ((vma) && (mi->usr + width > vma->vm_end)) {
		/* jump to the next VMA region */
		vma = find_vma(mm, vma->vm_end + 1);
	}
	if (!vma) {
		printk(KERN_ERR "Failed to get the vma region for "
			"user buffer.\n");
		goto fault;
	}

	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		write = 1;

	tmp = mi->usr;
	for (i = 0; i < mi->num_pg; i++) {
		if (get_user_pages(curr_task, mm, tmp, 1, write, 1, &page,
									NULL)) {
			if (page_count(page) < 1) {
				printk(KERN_ERR "Bad page count from"
							"get_user_pages()\n");
			}
			mi->pg_ptr[i] = (u32)page;
			mem[i] = page_to_phys(page);
			tmp += PAGE_SIZE;
		} else {
			printk(KERN_ERR "get_user_pages() failed\n");
			goto fault;
		}
	}
	up_read(&mm->mmap_sem);

	/* Ensure the data reaches to main memory before PAT refill */
	wmb();

	if (refill_pat(TMM(fmt), &mi->area, mem))
		goto fault;

	res = 0;
	*info = mi;
	goto done;
fault:
	up_read(&mm->mmap_sem);
done:
	if (res) {
		mutex_lock(&mtx);
		_m_free(mi);
		mutex_unlock(&mtx);
	}
	kfree(mem);
	return res;
}

s32 tiler_mapx(struct tiler_block_t *blk, enum tiler_fmt fmt, u32 gid,
				pid_t pid, u32 usr_addr)
{
	struct mem_info *mi;
	struct process_info *pi;
	s32 res;

	if (!blk || blk->phys)
		BUG();

	pi = __get_pi(pid, true);
	if (!pi)
		return -ENOMEM;

	res = map_block(fmt, blk->width, blk->height, blk->key, gid, pi, &mi,
								usr_addr);
	if (mi)
		blk->phys = mi->blk.phys;
	return res;

}
EXPORT_SYMBOL(tiler_mapx);

s32 tiler_map(struct tiler_block_t *blk, enum tiler_fmt fmt, u32 usr_addr)
{
	return tiler_mapx(blk, fmt, 0, current->tgid, usr_addr);
}
EXPORT_SYMBOL(tiler_map);

s32 free_block(u32 key, u32 id, struct process_info *pi)
{
	struct gid_info *gi = NULL;
	struct area_info *ai = NULL;
	struct mem_info *mi = NULL;
	s32 res = -ENOENT;

	mutex_lock(&mtx);

	/* find block in process list and free it */
	list_for_each_entry(gi, &pi->groups, by_pid) {
		/* is id is ssptr, we know if block is 1D or 2D by the address,
		   so we optimize lookup */
		if (!ssptr_id ||
		    TILER_GET_ACC_MODE(id) == TILFMT_PAGE) {
			list_for_each_entry(mi, &gi->onedim, by_area) {
				if (mi->blk.key == key && mi->blk.id == id) {
					_m_try_free(mi);
					res = 0;
					goto done;
				}
			}
		}

		if (!ssptr_id ||
		    TILER_GET_ACC_MODE(id) != TILFMT_PAGE) {
			list_for_each_entry(ai, &gi->areas, by_gid) {
				list_for_each_entry(mi, &ai->blocks, by_area) {
					if (mi->blk.key == key &&
					    mi->blk.id == id) {
						_m_try_free(mi);
						res = 0;
						goto done;
					}
				}
			}
		}
	}

done:
	mutex_unlock(&mtx);

	/* for debugging, we can set the PAT entries to DMM_LISA_MAP__0 */
	return res;
}

static s32 free_block_global(u32 key, u32 id)
{
	struct mem_info *mi;
	s32 res = -ENOENT;

	mutex_lock(&mtx);

	/* find block in global list and free it */
	list_for_each_entry(mi, &blocks, global) {
		if (mi->blk.key == key && mi->blk.id == id) {
			_m_try_free(mi);
			res = 0;
			break;
		}
	}
	mutex_unlock(&mtx);

	/* for debugging, we can set the PAT entries to DMM_LISA_MAP__0 */
	return res;
}

s32 tiler_free(struct tiler_block_t *blk)
{
	return free_block_global(blk->key, blk->id);
}
EXPORT_SYMBOL(tiler_free);

s32 find_block(u32 sys_addr, struct tiler_block_info *blk)
{
	struct mem_info *i;
	struct tcm_pt pt;

	if (!is_tiler_addr(sys_addr))
		return -EFAULT;

	if (get_area(sys_addr, &pt))
		return -EFAULT;

	list_for_each_entry(i, &blocks, global) {
		if (tiler_fmt(&i->blk) == TILER_GET_ACC_MODE(sys_addr) &&
		    tcm_is_in(pt, i->area))
			goto found;
	}
	return -EFAULT;

found:
	blk->ptr = NULL;
	blk->fmt = TILER_GET_ACC_MODE(i->blk.phys);
	blk->ssptr = i->blk.phys;

	if (blk->fmt == TILFMT_PAGE) {
		blk->dim.len = i->blk.width;
		blk->stride = 0;
		blk->group_id = ((struct gid_info *) i->parent)->gid;
	} else {
		blk->stride = tiler_vstride(&i->blk);
		blk->dim.area.width = i->blk.width;
		blk->dim.area.height = i->blk.height;
		blk->group_id = ((struct area_info *) i->parent)->gi->gid;
	}
	blk->id = i->blk.id;
	blk->key = i->blk.key;
	blk->offs = i->blk.phys & ~PAGE_MASK;
	blk->align = 0;
	return 0;
}

static s32 alloc_block(enum tiler_fmt fmt, u32 width, u32 height,
		u32 align, u32 offs, u32 key, u32 gid, struct process_info *pi,
		struct mem_info **info);

/* we have two algorithms for packing nv12 blocks */

/* we want to find the most effective packing for the smallest area */

/* layout reserved 2d areas in a larger area */
/* NOTE: band, w, h, a(lign), o(ffs) is in slots */
static s32 reserve_2d(enum tiler_fmt fmt, u16 n, u16 w, u16 h, u16 band,
		      u16 align, u16 offs, struct gid_info *gi,
		      struct list_head *pos)
{
	u16 x, x0, e = ALIGN(w, align), w_res = (n - 1) * e + w;
	struct mem_info *mi = NULL;
	struct area_info *ai = NULL;

	printk(KERN_INFO "packing %u %u buffers into %u width\n",
	       n, w, w_res);

	/* calculate dimensions, band, offs and alignment in slots */
	/* reserve an area */
	ai = area_new_m(ALIGN(w_res + offs, max(band, align)), h,
			max(band, align), TCM(fmt), gi);
	if (!ai)
		return -ENOMEM;

	/* lay out blocks in the reserved area */
	for (n = 0, x = offs; x < w_res; x += e, n++) {
		/* reserve a block struct */
		mi = kmalloc(sizeof(*mi), GFP_KERNEL);
		if (!mi)
			break;

		memset(mi, 0, sizeof(*mi));
		x0 = ai->area.p0.x + x;
		_m_add2area(mi, ai, x0, w, &ai->blocks);
		list_add(&mi->global, pos);
	}

	mutex_unlock(&mtx);
	return n;
}

/* reserve nv12 blocks if standard allocator is inefficient */
/* TILER is designed so that a (w * h) * 8bit area is twice as wide as a
   (w/2 * h/2) * 16bit area.  Since having pairs of such 8-bit and 16-bit
   blocks is a common usecase for TILER, we optimize packing these into a
   TILER area */
static s32 pack_nv12(int n, u16 w, u16 w1, u16 h, struct gid_info *gi,
		     u8 *p)
{
	u16 wh = (w1 + 1) >> 1, width, x0;
	int m;

	struct mem_info *mi = NULL;
	struct area_info *ai = NULL;
	struct list_head *pos;

	/* reserve area */
	ai = area_new_m(w, h, 64, TCM(TILFMT_8BIT), gi);
	if (!ai)
		return -ENOMEM;

	/* lay out blocks in the reserved area */
	for (m = 0; m < 2 * n; m++) {
		width =	(m & 1) ? wh : w1;
		x0 = ai->area.p0.x + *p++;

		/* get insertion head */
		list_for_each(pos, &ai->blocks) {
			mi = list_entry(pos, struct mem_info, by_area);
			if (mi->area.p0.x > x0)
				break;
		}

		/* reserve a block struct */
		mi = kmalloc(sizeof(*mi), GFP_KERNEL);
		if (!mi)
			break;

		memset(mi, 0, sizeof(*mi));

		_m_add2area(mi, ai, x0, width, pos);
		list_add(&mi->global, &gi->reserved);
	}

	mutex_unlock(&mtx);
	return n;
}

static inline u32 nv12_eff(u16 n, u16 w, u16 area, u16 n_need)
{
	/* rank by total area needed first */
	return 0x10000000 - DIV_ROUND_UP(n_need, n) * area * 32 +
		/* then by efficiency */
		1024 * n * ((w * 3 + 1) >> 1) / area;
}

static void reserve_nv12(u32 n, u32 width, u32 height, u32 align, u32 offs,
			 u32 gid, struct process_info *pi)
{
	/* adjust alignment to at least 128 bytes (16-bit slot width) */
	u16 w, h, band, a = MAX(128, align), o = offs, eff_w;
	struct gid_info *gi;
	int res = 0, res2, i;
	u16 n_t, n_s, area_t, area_s;
	u8 packing[2 * 21];
	struct list_head reserved = LIST_HEAD_INIT(reserved);
	struct mem_info *mi, *mi_;
	bool can_together = TMM(TILFMT_8BIT) == TMM(TILFMT_16BIT);

	/* Check input parameters for correctness, and support */
	if (!width || !height || !n ||
	    offs >= (align ? : default_align) || offs & 1 ||
	    align >= PAGE_SIZE || TCM(TILFMT_8BIT) != TCM(TILFMT_16BIT) ||
	    n > TILER_WIDTH * TILER_HEIGHT / 2)
		return;

	/* calculate dimensions, band, offs and alignment in slots */
	if (__analize_area(TILFMT_8BIT, width, height, &w, &h, &band, &a, &o,
									NULL))
		return;

	/* get group context */
	mutex_lock(&mtx);
	gi = _m_get_gi(pi, gid);
	mutex_unlock(&mtx);
	if (!gi)
		return;

	eff_w = ALIGN(w, a);

	for (i = 0; i < n && res >= 0; i += res) {
		/* check packing separately vs together */
		n_s = nv12_separate(o, w, a, n - i, &area_s);
		if (can_together)
			n_t = nv12_together(o, w, a, n - i, &area_t, packing);
		else
			n_t = 0;

		/* pack based on better efficiency */
		res = -1;
		if (!can_together ||
			nv12_eff(n_s, w, area_s, n - i) >
			nv12_eff(n_t, w, area_t, n - i)) {
			/* pack separately */

			res = reserve_2d(TILFMT_8BIT, n_s, w, h, band, a, o, gi,
					 &reserved);

			/* only reserve 16-bit blocks if 8-bit was successful,
			   as we will try to match 16-bit areas to an already
			   reserved 8-bit area, and there is no guarantee that
			   an unreserved 8-bit area will match the offset of
			   a singly reserved 16-bit area. */
			res2 = (res < 0 ? res :
				reserve_2d(TILFMT_16BIT, n_s, (w + 1) / 2, h,
				band / 2, a / 2, o / 2, gi, &reserved));
			if (res2 < 0 || res != res2) {
				/* clean up */
				mutex_lock(&mtx);
				list_for_each_entry_safe(mi, mi_, &reserved,
									global)
					_m_free(mi);
				mutex_unlock(&mtx);
				res = -1;
			} else {
				/* add list to reserved */
				mutex_lock(&mtx);
				list_splice_init(&reserved, &gi->reserved);
				mutex_unlock(&mtx);
			}
		}

		/* if separate packing failed, still try to pack together */
		if (res < 0 && can_together && n_t) {
			/* pack together */
			res = pack_nv12(n_t, area_t, w, h, gi, packing);
		}
	}

	mutex_lock(&mtx);
	gi->refs--;
	_m_try_free_group(gi);
	mutex_unlock(&mtx);
}

/* reserve 2d blocks (if standard allocator is inefficient) */
static void reserve_blocks(u32 n, enum tiler_fmt fmt, u32 width, u32 height,
			   u32 align, u32 offs, u32 gid,
			   struct process_info *pi)
{
	u32 til_width, bpp, bpt, res = 0, i;
	u16 o = offs, a = align, band, w, h, e, n_try;
	struct gid_info *gi;

	/* Check input parameters for correctness, and support */
	if (!width || !height || !n ||
	    align > PAGE_SIZE || offs >= (align ? : default_align) ||
	    fmt < TILFMT_8BIT || fmt > TILFMT_32BIT)
		return;

	/* tiler page width in pixels, bytes per pixel, tiler page in bytes */
	til_width = fmt == TILFMT_32BIT ? 32 : 64;
	bpp = 1 << (fmt - TILFMT_8BIT);
	bpt = til_width * bpp;

	/* check offset.  Also, if block is less than half the mapping window,
	   the default allocation is sufficient.  Also check for basic area
	   info. */
	if (width * bpp * 2 <= PAGE_SIZE ||
	    __analize_area(fmt, width, height, &w, &h, &band, &a, &o, NULL))
		return;

	/* get group id */
	mutex_lock(&mtx);
	gi = _m_get_gi(pi, gid);
	mutex_unlock(&mtx);
	if (!gi)
		return;

	/* effective width of a buffer */
	e = ALIGN(w, a);

	for (i = 0; i < n && res >= 0; i += res) {
		/* blocks to allocate in one area */
		n_try = MIN(n - i, TILER_WIDTH);
		tiler_best2pack(offs, w, e, band, &n_try, NULL);

		res = -1;
		while (n_try > 1) {
			res = reserve_2d(fmt, n_try, w, h, band, a, o, gi,
					 &gi->reserved);
			if (res >= 0)
				break;

			/* reduce n if failed to allocate area */
			n_try--;
		}
	}
	/* keep reserved blocks even if failed to reserve all */

	mutex_lock(&mtx);
	gi->refs--;
	_m_try_free_group(gi);
	mutex_unlock(&mtx);
}

s32 tiler_reservex(u32 n, enum tiler_fmt fmt, u32 width, u32 height,
		   u32 align, u32 offs, u32 gid, pid_t pid)
{
	struct process_info *pi = __get_pi(pid, true);

	if (pi)
		reserve_blocks(n, fmt, width, height, align, offs, gid, pi);
	return 0;
}
EXPORT_SYMBOL(tiler_reservex);

s32 tiler_reserve(u32 n, enum tiler_fmt fmt, u32 width, u32 height,
		  u32 align, u32 offs)
{
	return tiler_reservex(n, fmt, width, height, align, offs,
			      0, current->tgid);
}
EXPORT_SYMBOL(tiler_reserve);

/* reserve area for n identical buffers */
s32 tiler_reservex_nv12(u32 n, u32 width, u32 height, u32 align, u32 offs,
			u32 gid, pid_t pid)
{
	struct process_info *pi = __get_pi(pid, true);

	if (pi)
		reserve_nv12(n, width, height, align, offs, gid, pi);
	return 0;
}
EXPORT_SYMBOL(tiler_reservex_nv12);

s32 tiler_reserve_nv12(u32 n, u32 width, u32 height, u32 align, u32 offs)
{
	return tiler_reservex_nv12(n, width, height, align, offs,
				   0, current->tgid);
}
EXPORT_SYMBOL(tiler_reserve_nv12);

void unreserve_blocks(struct process_info *pi, u32 gid)
{
	struct gid_info *gi;
	struct mem_info *mi, *mi_;

	mutex_lock(&mtx);
	gi = _m_get_gi(pi, gid);
	if (!gi)
		goto done;
	/* we have the mutex, so no need to keep reference */
	gi->refs--;

	list_for_each_entry_safe(mi, mi_, &gi->reserved, global) {
		BUG_ON(mi->refs || mi->alloced);
		_m_free(mi);
	}
done:
	mutex_unlock(&mtx);
}

static s32 tiler_ioctl(struct inode *ip, struct file *filp, u32 cmd,
			unsigned long arg)
{
	pgd_t *pgd = NULL;
	pmd_t *pmd = NULL;
	pte_t *ptep = NULL, pte = 0x0;
	s32 r = -1;
	struct process_info *pi = filp->private_data;

	struct __buf_info *_b = NULL;
	struct tiler_buf_info buf_info = {0};
	struct tiler_block_info block_info = {0};
	struct mem_info *mi;

	switch (cmd) {
	case TILIOC_GBLK:
		if (copy_from_user(&block_info, (void __user *)arg,
					sizeof(block_info)))
			return -EFAULT;

		switch (block_info.fmt) {
		case TILFMT_PAGE:
			r = alloc_block(block_info.fmt, block_info.dim.len, 1,
					block_info.align, block_info.offs,
					block_info.key, block_info.group_id,
					pi, &mi);
			if (r)
				return r;
			break;
		case TILFMT_8BIT:
		case TILFMT_16BIT:
		case TILFMT_32BIT:
			r = alloc_block(block_info.fmt,
					block_info.dim.area.width,
					block_info.dim.area.height,
					block_info.align, block_info.offs,
					block_info.key, block_info.group_id,
					pi, &mi);
			if (r)
				return r;
			break;
		default:
			return -EINVAL;
		}

		if (mi) {
			block_info.id = mi->blk.id;
			block_info.stride = tiler_vstride(&mi->blk);
#ifdef CONFIG_TILER_EXPOSE_SSPTR
			block_info.ssptr = mi->blk.phys;
#endif
		}
		if (copy_to_user((void __user *)arg, &block_info,
					sizeof(block_info)))
			return -EFAULT;
		break;
	case TILIOC_FBLK:
	case TILIOC_UMBLK:
		if (copy_from_user(&block_info, (void __user *)arg,
					sizeof(block_info)))
			return -EFAULT;

		/* search current process first, then all processes */
		free_block(block_info.key, block_info.id, pi) ?
			free_block_global(block_info.key, block_info.id) : 0;

		/* free always succeeds */
		break;

	case TILIOC_GSSP:
		pgd = pgd_offset(current->mm, arg);
		if (!(pgd_none(*pgd) || pgd_bad(*pgd))) {
			pmd = pmd_offset(pgd, arg);
			if (!(pmd_none(*pmd) || pmd_bad(*pmd))) {
				ptep = pte_offset_map(pmd, arg);
				if (ptep) {
					pte = *ptep;
					if (pte_present(pte))
						return (pte & PAGE_MASK) |
							(~PAGE_MASK & arg);
				}
			}
		}
		/* va not in page table */
		return 0x0;
		break;
	case TILIOC_MBLK:
		if (copy_from_user(&block_info, (void __user *)arg,
					sizeof(block_info)))
			return -EFAULT;

		if (!block_info.ptr)
			return -EFAULT;

		r = map_block(block_info.fmt, block_info.dim.len, 1,
			      block_info.key, block_info.group_id, pi,
			      &mi, (u32)block_info.ptr);
		if (r)
			return r;

		if (mi) {
			block_info.id = mi->blk.id;
			block_info.stride = tiler_vstride(&mi->blk);
#ifdef CONFIG_TILER_EXPOSE_SSPTR
			block_info.ssptr = mi->blk.phys;
#endif
		}
		if (copy_to_user((void __user *)arg, &block_info,
					sizeof(block_info)))
			return -EFAULT;
		break;
#ifndef CONFIG_TILER_SECURE
	case TILIOC_QBUF:
		if (!offset_lookup)
			return -EPERM;

		if (copy_from_user(&buf_info, (void __user *)arg,
					sizeof(buf_info)))
			return -EFAULT;

		mutex_lock(&mtx);
		list_for_each_entry(_b, &pi->bufs, by_pid) {
			if (buf_info.offset == _b->buf_info.offset) {
				if (copy_to_user((void __user *)arg,
					&_b->buf_info,
					sizeof(_b->buf_info))) {
					mutex_unlock(&mtx);
					return -EFAULT;
				} else {
					mutex_unlock(&mtx);
					return 0;
				}
			}
		}
		mutex_unlock(&mtx);
		return -EFAULT;
		break;
#endif
	case TILIOC_RBUF:
		_b = kmalloc(sizeof(*_b), GFP_KERNEL);
		if (!_b)
			return -ENOMEM;

		memset(_b, 0x0, sizeof(*_b));

		if (copy_from_user(&_b->buf_info, (void __user *)arg,
					sizeof(_b->buf_info))) {
			kfree(_b); return -EFAULT;
		}

		r = register_buf(_b, pi);
		if (r) {
			kfree(_b); return -EACCES;
		}

		if (copy_to_user((void __user *)arg, &_b->buf_info,
					sizeof(_b->buf_info))) {
			_m_unregister_buf(_b);
			return -EFAULT;
		}
		break;
	case TILIOC_URBUF:
		if (copy_from_user(&buf_info, (void __user *)arg,
					sizeof(buf_info)))
			return -EFAULT;

		mutex_lock(&mtx);
		/* buffer registration is per process */
		list_for_each_entry(_b, &pi->bufs, by_pid) {
			if (buf_info.offset == _b->buf_info.offset) {
				_m_unregister_buf(_b);
				mutex_unlock(&mtx);
				return 0;
			}
		}
		mutex_unlock(&mtx);
		return -EFAULT;
		break;
	case TILIOC_PRBLK:
		if (copy_from_user(&block_info, (void __user *)arg,
					sizeof(block_info)))
			return -EFAULT;

		if (block_info.fmt == TILFMT_8AND16) {
			reserve_nv12(block_info.key,
				     block_info.dim.area.width,
				     block_info.dim.area.height,
				     block_info.align,
				     block_info.offs,
				     block_info.group_id, pi);
		} else {
			reserve_blocks(block_info.key,
				       block_info.fmt,
				       block_info.dim.area.width,
				       block_info.dim.area.height,
				       block_info.align,
				       block_info.offs,
				       block_info.group_id, pi);
		}
		break;
	case TILIOC_URBLK:
		unreserve_blocks(pi, arg);
		break;
#ifndef CONFIG_TILER_SECURE
	case TILIOC_QBLK:
		if (!ssptr_lookup)
			return -EPERM;

		if (copy_from_user(&block_info, (void __user *)arg,
					sizeof(block_info)))
			return -EFAULT;

		if (find_block(block_info.ssptr, &block_info))
			return -EFAULT;

		if (copy_to_user((void __user *)arg, &block_info,
			sizeof(block_info)))
			return -EFAULT;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0x0;
}

s32 alloc_block(enum tiler_fmt fmt, u32 width, u32 height,
		u32 align, u32 offs, u32 key, u32 gid, struct process_info *pi,
		struct mem_info **info)
{
	struct mem_info *mi = NULL;
	struct gid_info *gi = NULL;

	*info = NULL;

	/* only support up to page alignment */
	if (align > PAGE_SIZE || offs >= (align ? : default_align) || !pi)
		return -EINVAL;

	/* get group context */
	mutex_lock(&mtx);
	gi = _m_get_gi(pi, gid);
	mutex_unlock(&mtx);

	if (!gi)
		return -ENOMEM;

	/* reserve area in tiler container */
	mi = __get_area(fmt, width, height, align, offs, gi);
	if (!mi) {
		mutex_lock(&mtx);
		gi->refs--;
		_m_try_free_group(gi);
		mutex_unlock(&mtx);
		return -ENOMEM;
	}

	mi->blk.width = width;
	mi->blk.height = height;
	mi->blk.key = key;
	if (ssptr_id) {
		mi->blk.id = mi->blk.phys;
	} else {
		mutex_lock(&mtx);
		mi->blk.id = _m_get_id();
		mutex_unlock(&mtx);
	}

	/* allocate and map if mapping is supported */
	if (tmm_can_map(TMM(fmt))) {
		mi->num_pg = tcm_sizeof(mi->area);

		mi->mem = tmm_get(TMM(fmt), mi->num_pg);
		if (!mi->mem)
			goto cleanup;

		/* Ensure the data reaches to main memory before PAT refill */
		wmb();

		/* program PAT */
		if (refill_pat(TMM(fmt), &mi->area, mi->mem))
			goto cleanup;
	}
	*info = mi;
	return 0;

cleanup:
	mutex_lock(&mtx);
	_m_free(mi);
	mutex_unlock(&mtx);
	return -ENOMEM;

}

s32 tiler_allocx(struct tiler_block_t *blk, enum tiler_fmt fmt,
				u32 align, u32 offs, u32 gid, pid_t pid)
{
	struct mem_info *mi;
	struct process_info *pi;
	s32 res;

	if (!blk || blk->phys)
		BUG();

	pi = __get_pi(pid, true);
	if (!pi)
		return -ENOMEM;

	res = alloc_block(fmt, blk->width, blk->height, align, offs, blk->key,
								gid, pi, &mi);
	if (mi)
		blk->phys = mi->blk.phys;
	return res;
}
EXPORT_SYMBOL(tiler_allocx);

s32 tiler_alloc(struct tiler_block_t *blk, enum tiler_fmt fmt,
		u32 align, u32 offs)
{
	return tiler_allocx(blk, fmt, align, offs, 0, current->tgid);
}
EXPORT_SYMBOL(tiler_alloc);

static void __exit tiler_exit(void)
{
	struct process_info *pi = NULL, *pi_ = NULL;
	int i, j;

	mutex_lock(&mtx);

	/* free all process data */
	list_for_each_entry_safe(pi, pi_, &procs, list)
		_m_free_process_info(pi);

	/* all lists should have cleared */
	BUG_ON(!list_empty(&blocks));
	BUG_ON(!list_empty(&procs));
	BUG_ON(!list_empty(&orphan_onedim));
	BUG_ON(!list_empty(&orphan_areas));

	mutex_unlock(&mtx);

	/* close containers only once */
	for (i = TILFMT_8BIT; i <= TILFMT_MAX; i++) {
		/* remove identical containers (tmm is unique per tcm) */
		for (j = i + 1; j <= TILFMT_MAX; j++)
			if (TCM(i) == TCM(j)) {
				TCM_SET(j, NULL);
				TMM_SET(j, NULL);
			}

		tcm_deinit(TCM(i));
		tmm_deinit(TMM(i));
	}

	mutex_destroy(&mtx);
	platform_driver_unregister(&tiler_driver_ldm);
	cdev_del(&tiler_device->cdev);
	kfree(tiler_device);
	device_destroy(tilerdev_class, MKDEV(tiler_major, tiler_minor));
	class_destroy(tilerdev_class);
}

static s32 tiler_open(struct inode *ip, struct file *filp)
{
	struct process_info *pi = __get_pi(current->tgid, false);

	if (!pi)
		return -ENOMEM;

	filp->private_data = pi;
	return 0x0;
}

static s32 tiler_release(struct inode *ip, struct file *filp)
{
	struct process_info *pi = filp->private_data;

	mutex_lock(&mtx);
	/* free resources if last device in this process */
	if (0 == --pi->refs)
		_m_free_process_info(pi);

	mutex_unlock(&mtx);

	return 0x0;
}

static const struct file_operations tiler_fops = {
	.open    = tiler_open,
	.ioctl   = tiler_ioctl,
	.release = tiler_release,
	.mmap    = tiler_mmap,
};

static s32 __init tiler_init(void)
{
	dev_t dev  = 0;
	s32 r = -1;
	struct device *device = NULL;
	struct tcm_pt div_pt;
	struct tcm *sita = NULL;
	struct tmm *tmm_pat = NULL;

	/* check module parameters for correctness */
	if (default_align > PAGE_SIZE ||
	    default_align & (default_align - 1) ||
	    granularity < 1 || granularity > PAGE_SIZE ||
	    granularity & (granularity - 1))
		return -EINVAL;
#ifdef CONFIG_TILER_SECURE
	security = true;
	offset_lookup = ssptr_lookup = false;
#endif
	/* Allocate tiler container manager (we share 1 on OMAP4) */
	div_pt.x = TILER_WIDTH;   /* hardcoded default */
	div_pt.y = (3 * TILER_HEIGHT) / 4;
	sita = sita_init(TILER_WIDTH, TILER_HEIGHT, (void *)&div_pt);

	TCM_SET(TILFMT_8BIT, sita);
	TCM_SET(TILFMT_16BIT, sita);
	TCM_SET(TILFMT_32BIT, sita);
	TCM_SET(TILFMT_PAGE, sita);

	/* Allocate tiler memory manager (must have 1 unique TMM per TCM ) */
	tmm_pat = tmm_pat_init(0);
	TMM_SET(TILFMT_8BIT, tmm_pat);
	TMM_SET(TILFMT_16BIT, tmm_pat);
	TMM_SET(TILFMT_32BIT, tmm_pat);
	TMM_SET(TILFMT_PAGE, tmm_pat);

	tiler_device = kmalloc(sizeof(*tiler_device), GFP_KERNEL);
	if (!tiler_device || !sita || !tmm_pat) {
		r = -ENOMEM;
		goto error;
	}

	memset(tiler_device, 0x0, sizeof(*tiler_device));
	if (tiler_major) {
		dev = MKDEV(tiler_major, tiler_minor);
		r = register_chrdev_region(dev, 1, "tiler");
	} else {
		r = alloc_chrdev_region(&dev, tiler_minor, 1, "tiler");
		tiler_major = MAJOR(dev);
	}

	cdev_init(&tiler_device->cdev, &tiler_fops);
	tiler_device->cdev.owner = THIS_MODULE;
	tiler_device->cdev.ops   = &tiler_fops;

	r = cdev_add(&tiler_device->cdev, dev, 1);
	if (r)
		printk(KERN_ERR "cdev_add():failed\n");

	tilerdev_class = class_create(THIS_MODULE, "tiler");

	if (IS_ERR(tilerdev_class)) {
		printk(KERN_ERR "class_create():failed\n");
		goto error;
	}

	device = device_create(tilerdev_class, NULL, dev, NULL, "tiler");
	if (device == NULL)
		printk(KERN_ERR "device_create() fail\n");

	r = platform_driver_register(&tiler_driver_ldm);

	mutex_init(&mtx);
	INIT_LIST_HEAD(&blocks);
	INIT_LIST_HEAD(&procs);
	INIT_LIST_HEAD(&orphan_areas);
	INIT_LIST_HEAD(&orphan_onedim);

error:
	/* TODO: error handling for device registration */
	if (r) {
		kfree(tiler_device);
		tcm_deinit(sita);
		tmm_deinit(tmm_pat);
	}

	return r;
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Sin <davidsin@ti.com>");
MODULE_AUTHOR("Lajos Molnar <molnar@ti.com>");
module_init(tiler_init);
module_exit(tiler_exit);
