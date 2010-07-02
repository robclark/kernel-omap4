#ifndef __TILER_PACK_H__
#define __TILER_PACK_H__

#include <linux/kernel.h>
#include "tcm.h"

extern const struct file_operations tiler_fops;

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
	int refs;			/* instances directly using this ptr */
	struct process_info *pi;	/* parent */
};

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
	int refs;			/* number of times referenced */
	bool alloced;			/* still alloced */

	struct list_head by_area;	/* blocks in the same area / 1D */
	void *parent;			/* area info for 2D, else group info */
};

/* tiler-main.c */
struct mem_info *find_n_lock(u32 key, u32 id, struct gid_info *gi);
s32 alloc_block(enum tiler_fmt fmt, u32 width, u32 height,
		u32 align, u32 offs, u32 key, u32 gid, struct process_info *pi,
		struct mem_info **info);
s32 map_block(enum tiler_fmt fmt, u32 width, u32 height,
		     u32 key, u32 gid, struct process_info *pi,
		     struct mem_info **info, u32 usr_addr);

void destroy_group(struct gid_info *pi);

struct mem_info *find_block_by_ssptr(u32 sys_addr);
void fill_block_info(struct mem_info *i, struct tiler_block_info *blk);

void unlock_n_free(struct mem_info *mi, bool free);

s32 __analize_area(enum tiler_fmt fmt, u32 width, u32 height,
			  u16 *x_area, u16 *y_area, u16 *band,
			  u16 *align, u16 *offs, u16 *in_offs);

struct gid_info *get_gi(struct process_info *pi, u32 gid);
void release_gi(struct gid_info *gi);

void add_reserved_blocks(struct list_head *reserved, struct gid_info *gi);
void release_blocks(struct list_head *reserved);

/* tiler-reserve.c */
void reserve_nv12(u32 n, u32 width, u32 height, u32 align, u32 offs,
			 u32 gid, struct process_info *pi, bool can_together);
void reserve_blocks(u32 n, enum tiler_fmt fmt, u32 width, u32 height,
			   u32 align, u32 offs, u32 gid,
			   struct process_info *pi);
void unreserve_blocks(struct process_info *pi, u32 gid);

s32 reserve_2d(enum tiler_fmt fmt, u16 n, u16 w, u16 h, u16 band,
		      u16 align, u16 offs, struct gid_info *gi,
		      struct list_head *pos);
s32 pack_nv12(int n, u16 w, u16 w1, u16 h, struct gid_info *gi,
		     u8 *p);
void destroy_processes(void);
void tiler_proc_init(void);
/*** __get_area */
u32 tiler_get_address(struct tiler_view_orient orient,
			enum tiler_fmt fmt, u32 x, u32 y);
/*** find block */
void tiler_get_natural_xy(u32 tsptr, u32 *x, u32 *y);

#endif
