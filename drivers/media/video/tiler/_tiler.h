#ifndef __TILER_PACK_H__
#define __TILER_PACK_H__

#include <linux/kernel.h>
#include "tcm.h"

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

struct tiler_ops {
	/* block operations */
	s32 (*alloc) (enum tiler_fmt fmt, u32 width, u32 height,
			u32 align, u32 offs, u32 key,
			u32 gid, struct process_info *pi,
			struct mem_info **info);
	s32 (*map) (enum tiler_fmt fmt, u32 width, u32 height,
			u32 key, u32 gid, struct process_info *pi,
			struct mem_info **info, u32 usr_addr);

	struct mem_info * (*lock) (u32 key, u32 id, struct gid_info *gi);
	void (*unlock_free) (struct mem_info *mi, bool free);

	struct mem_info * (*get_by_ssptr) (u32 sys_addr);
	void (*describe) (struct mem_info *i, struct tiler_block_info *blk);

	void (*add_reserved) (struct list_head *reserved, struct gid_info *gi);
	void (*release) (struct list_head *reserved);

	/* group operations */
	struct gid_info * (*get_gi) (struct process_info *pi, u32 gid);
	void (*release_gi) (struct gid_info *gi);
	void (*destroy_group) (struct gid_info *pi);


	s32 (*analize) (enum tiler_fmt fmt, u32 width, u32 height,
				  u16 *x_area, u16 *y_area, u16 *band,
				  u16 *align, u16 *offs, u16 *in_offs);


	/* process operations */
	void (*cleanup) (void);

	/* geometry operations */
	void (*xy) (u32 tsptr, u32 *x, u32 *y);
	u32 (*addr) (struct tiler_view_orient orient, enum tiler_fmt fmt,
			u32 x, u32 y);

	/* reservation operations */
	void (*reserve_nv12) (u32 n, u32 width, u32 height, u32 align, u32 offs,
			u32 gid, struct process_info *pi, bool can_together);
	void (*reserve) (u32 n, enum tiler_fmt fmt, u32 width, u32 height,
			u32 align, u32 offs, u32 gid, struct process_info *pi);
	void (*unreserve) (u32 gid, struct process_info *pi);

	s32 (*lay_2d) (enum tiler_fmt fmt, u16 n, u16 w, u16 h, u16 band,
				u16 align, u16 offs, struct gid_info *gi,
				struct list_head *pos);
	s32 (*lay_nv12) (int n, u16 w, u16 w1, u16 h, struct gid_info *gi,
									u8 *p);
	const struct file_operations *fops;
};

void tiler_iface_init(struct tiler_ops *tiler);
void tiler_geom_init(struct tiler_ops *tiler);
void tiler_reserve_init(struct tiler_ops *tiler);

#endif
