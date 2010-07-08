/*
 * tcm_sita.c
 *
 * Author: Ravi Ramachandra <r.ramachandra@ti.com>
 *
 * SImple Tiler Allocator (SiTA): 2D and 1D allocation(reservation) algorithm
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
 *
 */
#include <linux/slab.h>

#include "_tcm-sita.h"
#include "tcm-sita.h"

#define TCM_ALG_NAME "tcm_sita"
#include "tcm-utils.h"

#define X_SCAN_LIMITER	1
#define Y_SCAN_LIMITER	1

#define ALIGN_DOWN(value, align) ((value) & ~((align) - 1))

/* Individual selection criteria for different scan areas */
static s32 CR_L2R_T2B = CR_BIAS_HORIZONTAL;
static s32 CR_R2L_T2B = CR_DIAGONAL_BALANCE;
#ifdef SCAN_BOTTOM_UP
static s32 CR_R2L_B2T = CR_FIRST_FOUND;
static s32 CR_L2R_B2T = CR_DIAGONAL_BALANCE;
#endif

/*********************************************
 *	TCM API - Sita Implementation
 *********************************************/
static s32 sita_reserve_2d(struct tcm *tcm, u16 h, u16 w, u8 align,
		    struct tcm_area *area);
static s32 sita_reserve_1d(struct tcm *tcm, u32 slots, struct tcm_area
		    *area);
static s32 sita_free(struct tcm *tcm, struct tcm_area *to_be_removed_area);
static void sita_deinit(struct tcm *tcm);

/*********************************************
 *	Main Scanner functions
 *********************************************/
static s32 scan_areas_and_find_fit(struct tcm *tcm, u16 w, u16 h, u16 stride,
				   struct tcm_area *area);

static s32 scan_l2r_t2b(struct tcm *tcm, u16 w, u16 h, u16 stride,
			struct tcm_area *field, struct tcm_area *area);

static s32 scan_r2l_t2b(struct tcm *tcm, u16 w, u16 h, u16 stride,
			struct tcm_area *field, struct tcm_area *area);

#ifdef SCAN_BOTTOM_UP
static s32 scan_l2r_b2t(struct tcm *tcm, u16 w, u16 h, u16 stride,
			struct tcm_area *field, struct tcm_area *area);

static s32 scan_r2l_b2t(struct tcm *tcm, u16 w, u16 h, u16 stride,
			struct tcm_area *field, struct tcm_area *area);
#endif
static s32 scan_r2l_b2t_one_dim(struct tcm *tcm, u32 num_pages,
			struct tcm_area *field, struct tcm_area *area);

/*********************************************
 *	Support Infrastructure Methods
 *********************************************/
static s32 check_fit_r_and_b(struct tcm *tcm, u16 w, u16 h, u16 left_x,
			     u16 top_y);

static s32 check_fit_r_one_dim(struct tcm *tcm, u16 x, u16 y, u32 num_pages,
			       u16 *busy_x, u16 *busy_y);

static s32 update_candidate(struct tcm *tcm, u16 x0, u16 y0, u16 w, u16 h,
			     struct tcm_area *field, s32 criteria,
			     struct score *best);

static void get_nearness_factor(struct tcm_area *field,
		       struct tcm_area *candidate, struct nearness_factor *nf);

static s32 get_busy_neigh_stats(struct tcm *tcm, u16 width, u16 height,
			struct tcm_area *top_left_corner,
			struct neighbour_stats *neighbour_stat);

static void fill_1d_area(struct tcm *tcm,
			struct tcm_area *area, struct tcm_area *parent);

static void fill_2d_area(struct tcm *tcm,
		       struct tcm_area *area, struct tcm_area *parent);

static s32 move_left(struct tcm *tcm, u16 x, u16 y, u32 num_pages,
		     u16 *xx, u16 *yy);
static s32 move_right(struct tcm *tcm, u16 x, u16 y, u32 num_pages,
		      u16 *xx, u16 *yy);
/*********************************************/

/*********************************************
 *	Utility Methods
 *********************************************/

#if 0
static
void dump_list_entries(struct list_head *head)
{
	struct area_spec *elem = NULL;

	P1("Printing List Entries:\n");

	list_for_each_entry(elem, head, list) {
		printk(KERN_NOTICE "%dD:" AREA_FMT "\n", elem->area.type,
					AREA(elem->area));
	}

	P1("List Finished\n");
}

static
s32 dump_neigh_stats(struct neighbour_stats *neighbour)
{
	P1("Top  Occ:Boundary  %d:%d\n", neighbour->top_occupied,
						neighbour->top_boundary);
	P1("Bot  Occ:Boundary  %d:%d\n", neighbour->bottom_occupied,
						neighbour->bottom_boundary);
	P1("Left Occ:Boundary  %d:%d\n", neighbour->left_occupied,
						neighbour->left_boundary);
	P1("Rigt Occ:Boundary  %d:%d\n", neighbour->right_occupied,
						neighbour->right_boundary);
	return 0;
}
#endif

struct tcm *sita_init(u16 width, u16 height, struct tcm_pt *attr)
{
	struct tcm *tcm = NULL;
	struct sita_pvt *pvt = NULL;
	struct tcm_area area = {0};
	s32 i = 0;

	if (width == 0 || height == 0)
		goto error;

	tcm = kmalloc(sizeof(*tcm), GFP_KERNEL);
	pvt = kmalloc(sizeof(*pvt), GFP_KERNEL);
	if (!tcm || !pvt)
		goto error;

	memset(tcm, 0, sizeof(*tcm));
	memset(pvt, 0, sizeof(*pvt));

	/* Updating the pointers to SiTA implementation APIs */
	tcm->height = height;
	tcm->width = width;
	tcm->reserve_2d = sita_reserve_2d;
	tcm->reserve_1d = sita_reserve_1d;
	tcm->free = sita_free;
	tcm->deinit = sita_deinit;
	tcm->pvt = (void *)pvt;

	pvt->height = height;
	pvt->width = width;

	mutex_init(&(pvt->mtx));

	/* Creating tam map */
	pvt->map = kmalloc(sizeof(*pvt->map) * pvt->width, GFP_KERNEL);

	if (!pvt->map)
		goto error;

	for (i = 0; i < pvt->width; i++) {
		pvt->map[i] =
			kmalloc(sizeof(**pvt->map) * pvt->height,
								GFP_KERNEL);
		if (pvt->map[i] == NULL) {
			while (i--)
				kfree(pvt->map[i]);
			kfree(pvt->map);
			goto error;
		}
	}

	if (attr && attr->x <= pvt->width && attr->y <= pvt->height) {
		pvt->div_pt.x = attr->x;
		pvt->div_pt.y = attr->y;

	} else {
		/* Defaulting to 3:1 ratio on width for 2D area split */
		/* Defaulting to 3:1 ratio on height for 2D and 1D split */
		pvt->div_pt.x = (pvt->width * 3) / 4;
		pvt->div_pt.y = (pvt->height * 3) / 4;
	}

	area.p1.x = width - 1;
	area.p1.y = height - 1;

	mutex_lock(&(pvt->mtx));
	fill_2d_area(tcm, &area, NULL);
	mutex_unlock(&(pvt->mtx));
	return tcm;

error:
	kfree(tcm);
	kfree(pvt);
	return NULL;
}

static void sita_deinit(struct tcm *tcm)
{
	struct sita_pvt *pvt = NULL;
	struct tcm_area area = {0};
	s32 i = 0;

	pvt = (struct sita_pvt *)tcm->pvt;
	if (pvt) {
		area.p1.x = pvt->width - 1;
		area.p1.y = pvt->height - 1;

		mutex_lock(&(pvt->mtx));
		fill_2d_area(tcm, &area, NULL);
		mutex_unlock(&(pvt->mtx));

		mutex_destroy(&(pvt->mtx));

		for (i = 0; i < pvt->height; i++) {
			kfree(pvt->map[i]);
			pvt->map[i] = NULL;
		}
		kfree(pvt->map);
		pvt->map = NULL;
		kfree(pvt);
	}
}

/**
 * @description: Allocate 1d pages if the required number of pages are
 * available in the container
 *
 * @input:num_pages to be allocated
 *
 * @return 0 on success, non-0 error value on failure. On success
 * area contain co-ordinates of start and end Tiles(inclusive)
 */
static s32 sita_reserve_1d(struct tcm *tcm, u32 num_pages,
			   struct tcm_area *area)
{
	s32 ret = 0;
	struct tcm_area field = {0};
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	area->is2d = false;

	mutex_lock(&(pvt->mtx));
#ifdef RESTRICT_1D
	/* scan within predefined 1D boundary */
	assign(&field, pvt->width - 1, pvt->height - 1, 0, pvt->div_pt.y);
#else
	/* Scanning entire container */
	assign(&field, pvt->width - 1, pvt->height - 1, 0, 0);
#endif
	ret = scan_r2l_b2t_one_dim(tcm, num_pages,
				   &field, area);
	/* There is not much to select, we pretty much give the first one
	   which accomodates */
	if (!ret)
		/* update map */
		fill_1d_area(tcm, area, area);
	else
		/* otherwise, invalidate area */
		area->tcm = NULL;

	mutex_unlock(&(pvt->mtx));
	return ret;
}

/**
 * @description: Allocate 2d area on availability in the container
 *
 * @input:'w'idth and 'h'eight of the 2d area, 'align'ment specification
 *
 * @return 0 on success, non-0 error value on failure. On success
 * area contain co-ordinates of TL corner Tile and BR corner Tile of
 * the rectangle (inclusive)
 */
static s32 sita_reserve_2d(struct tcm *tcm, u16 h, u16 w, u8 align,
			   struct tcm_area *area)
{
	s32 ret = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	/* we only support 1, 32 and 64 as alignment */
	u16 stride = align <= 1 ? 1 : align <= 32 ? 32 : 64;

	area->is2d = true;

	/* align must be 2 power */
	if (align & (align - 1) || align > 64)
		return -EINVAL;

	mutex_lock(&(pvt->mtx));
	ret = scan_areas_and_find_fit(tcm, w, h, stride, area);
	if (!ret)
		/* update map */
		fill_2d_area(tcm, area, area);
	else
		/* otherwise, invalidate area */
		area->tcm = NULL;

	mutex_unlock(&(pvt->mtx));
	return ret;
}

/**
 * @description: unreserve a previously allocated 2d or 1D allocation
 *
 * @input:'area' specification: for 2D this should contain
 * TL Corner and BR Corner of the 2D area, or for 1D allocation this should
 * contain the start and end Tiles
 *
 * The tiles corresponding to the area are marked 'NOT_OCCUPIED'
 */
static s32 sita_free(struct tcm *tcm, struct tcm_area *area)
{
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	mutex_lock(&(pvt->mtx));

	/* check that this is in fact an existing area */
	BUG_ON(pvt->map[area->p0.x][area->p0.y] != area ||
		pvt->map[area->p1.x][area->p1.y] != area);

	/* Clear the contents of the associated tiles in the map */
	if (area->is2d)
		fill_2d_area(tcm, area, NULL);
	else
		fill_1d_area(tcm, area, NULL);

	mutex_unlock(&(pvt->mtx));

	return 0;
}

/**
 * @description: raster scan right to left from top to bottom; find if there is
 * a free area to fit a given w x h inside the 'scan area'. If there is a free
 * area, then adds to maybes candidates, which later is sent for selection
 * as per pre-defined criteria.
 *
 * @input:'w x h' width and height of the allocation area.
 * 'stride' - 64/32/None for start address alignment
 * 'field' - area in which the scan operation should take place
 *
 * @return 0 on success, non-0 error value on failure. On success
 * the 'area' area contains TL and BR corners of the allocated area
 *
 */
static s32 scan_r2l_t2b(struct tcm *tcm, u16 w, u16 h, u16 stride,
		 struct tcm_area *field, struct tcm_area *area)
{
	s32 xx = 0, yy = 0, done = 0;
	s16 start_x = -1, end_x = -1, start_y = -1, end_y = -1;
	s16 found_x = -1, found_y = -1;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct score best = {{0}, {0}, {0}, 0};

	PA(2, "scan_r2l_t2b:", field);

	start_x = field->p0.x;
	end_x = field->p1.x;
	start_y = field->p0.y;
	end_y = field->p1.y;

	/* check scan area co-ordinates */
	if (field->p0.x < field->p1.x ||
	    field->p1.y < field->p0.y)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (w > INCL_LEN(start_x, end_x) || h > INCL_LEN(end_y, start_y))
		return -ENOSPC;

	/* adjust start_x and end_y, as allocation would not fit beyond */
	start_x = ALIGN_DOWN(start_x - w + 1, stride); /* - 1 to be inclusive */
	end_y = end_y - h + 1;

	/* check if allocation would still fit in scan area */
	if (start_x < end_x)
		return -ENOSPC;

	P2("ali=%d x=%d..%d y=%d..%d", stride, start_x, end_x, start_y, end_y);

	/*
	 * Start scanning: These scans are always inclusive ones  so if we are
	 * given a start x = 0 is a valid value  so if we have a end_x = 255,
	 * 255th element is also checked
	 */
	for (yy = start_y; yy <= end_y && !done; yy++) {
		for (xx = start_x; xx >= end_x; xx -= stride) {
			if (!pvt->map[xx][yy]) {
				if (check_fit_r_and_b(tcm, w, h, xx, yy)) {
					P3("found shoulder: %d,%d", xx, yy);
					found_x = xx;
					found_y = yy;
					/* Insert this candidate, it is just a
						co-ordinate, reusing Area */
					done = update_candidate(tcm, xx, yy, w,
						h, field, CR_R2L_T2B, &best);
					if (done)
						break;

#ifdef X_SCAN_LIMITER
					/* change upper x bound */
					end_x = xx + 1;
#endif
					break;
				}
			} else {
				/* Optimization required only for Non Aligned,
				Aligned anyways skip by 32/64 tiles at a time */
				if (stride == 1 &&
				    pvt->map[xx][yy]->is2d) {
					xx = pvt->map[xx][yy]->p0.x;
					P3("moving to: %d,%d", xx, yy);
				}
			}
		}

		/* if you find a free area shouldering the given scan area on
		   then we can break */
#ifdef Y_SCAN_LIMITER
		if (found_x == start_x)
			break;
#endif
	}

	if (!best.a.tcm)
		return -ENOSPC;

	assign(area, best.a.p0.x, best.a.p0.y, best.a.p1.x, best.a.p1.y);
	return 0;
}

#ifdef SCAN_BOTTOM_UP
/**
 * @description: raster scan right to left from bottom to top; find if there is
 * a free area to fit a given w x h inside the 'scan area'. If there is a free
 * area, then adds to maybes candidates, which later is sent for selection
 * as per pre-defined criteria.
 *
 * @input:'w x h' width and height of the allocation area.
 * 'stride' - 64/32/None for start address alignment
 * 'field' - area in which the scan operation should take place
 *
 * @return 0 on success, non-0 error value on failure. On success
 * the 'area' area contains TL and BR corners of the allocated area
 *
 */
static s32 scan_r2l_b2t(struct tcm *tcm, u16 w, u16 h, u16 stride,
			struct tcm_area *field, struct tcm_area *area)
{
	/* TODO: Should I check scan area?
	 * Might have to take it as input during initialization
	 */
	s32 xx = 0, yy = 0, done = 0;
	s16 start_x = -1, end_x = -1, start_y = -1, end_y = -1;
	s16 found_x = -1, found_y = -1;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct score best = {{0}, {0}, {0}, 0};

	PA(2, "scan_r2l_b2t:", field);

	start_x = field->p0.x;
	end_x = field->p1.x;
	start_y = field->p0.y;
	end_y = field->p1.y;

	/* check scan area co-ordinates */
	if (field->p1.x < field->p0.x ||
	    field->p1.y < field->p0.y)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (w > INCL_LEN(start_x, end_x) || h > INCL_LEN(start_y, end_y))
		return -ENOSPC;

	/* adjust start_x and start_y, as allocation would not fit beyond */
	start_x = ALIGN_DOWN(start_x - w + 1, stride); /* + 1 to be inclusive */
	start_y = start_y - h + 1;

	/* check if allocation would still fit in scan area */
	if (start_x < end_x)
		return -ENOSPC;

	P2("ali=%d x=%d..%d y=%d..%d", stride, start_x, end_x, start_y, end_y);

	/*
	 * Start scanning: These scans are always inclusive ones  so if we are
	 * given a start x = 0 is a valid value  so if we have a end_x = 255,
	 * 255th element is also checked
	 */
	for (yy = start_y; yy >= end_y && !done; yy--) {
		for (xx = start_x; xx >= end_x; xx -= stride) {
			if (!pvt->map[xx][yy]) {
				if (check_fit_r_and_b(tcm, w, h, xx, yy)) {
					P3("found shoulder: %d,%d", xx, yy);
					found_x = xx;
					found_y = yy;
					/* Insert this candidate, it is just a
						co-ordinate, reusing Area */
					done = update_candidate(tcm, xx, yy, w,
						h, field, CR_R2L_B2T, &best);
					if (done)
						break;
#ifdef X_SCAN_LIMITER
					/* change upper x bound */
					end_x = xx + 1;
#endif
					break;
				}
			} else {
				/* Optimization required only for Non Aligned,
				Aligned anyways skip by 32/64 tiles at a time */
				if (stride == 1 &&
				    pvt->map[xx][yy]->is2d) {
					xx = pvt->map[xx][yy]->p0.x;
					P3("moving to: %d,%d", xx, yy);
				}
			}

		}

		/* if you find a free area shouldering the given scan area on
		   then we can break */
#ifdef Y_SCAN_LIMITER
		if (found_x == start_x)
			break;
#endif
	}

	if (!best.a.tcm)
		return -ENOSPC;

	assign(area, best.a.p0.x, best.a.p0.y, best.a.p1.x, best.a.p1.y);
	return 0;
}
#endif

/**
 * @description: raster scan left to right from top to bottom; find if there is
 * a free area to fit a given w x h inside the 'scan area'. If there is a free
 * area, then adds to maybes candidates, which later is sent for selection
 * as per pre-defined criteria.
 *
 * @input:'w x h' width and height of the allocation area.
 * 'stride' - 64/32/None for start address alignment
 * 'field' - area in which the scan operation should take place
 *
 * @return 0 on success, non-0 error value on failure. On success
 * the 'area' area contains TL and BR corners of the allocated area
 *
 */
s32 scan_l2r_t2b(struct tcm *tcm, u16 w, u16 h, u16 stride,
		 struct tcm_area *field, struct tcm_area *area)
{
	s32 xx = 0, yy = 0, done = 0;
	s16 start_x = -1, end_x = -1, start_y = -1, end_y = -1;
	s16 found_x = -1, found_y = -1;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct score best = {{0}, {0}, {0}, 0};

	PA(2, "scan_l2r_t2b:", field);

	start_x = field->p0.x;
	end_x = field->p1.x;
	start_y = field->p0.y;
	end_y = field->p1.y;

	/* check scan area co-ordinates */
	if (field->p1.x < field->p0.x ||
	    field->p1.y < field->p0.y)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (w > INCL_LEN(end_x, start_x) || h > INCL_LEN(end_y, start_y))
		return -ENOSPC;

	start_x = ALIGN(start_x, stride);

	/* check if allocation would still fit in scan area */
	if (w > INCL_LEN(end_x, start_x))
		return -ENOSPC;

	/* adjust end_x and end_y, as allocation would not fit beyond */
	end_x = end_x - w + 1; /* + 1 to be inclusive */
	end_y = end_y - h + 1;

	P2("ali=%d x=%d..%d y=%d..%d", stride, start_x, end_x, start_y, end_y);

	/*
	 * Start scanning: These scans are always inclusive ones  so if we are
	 * given a start x = 0 is a valid value  so if we have a end_x = 255,
	 * 255th element is also checked
	 */
	for (yy = start_y; yy <= end_y && !done; yy++) {
		for (xx = start_x; xx <= end_x; xx += stride) {
			/* if NOT occupied */
			if (!pvt->map[xx][yy]) {
				if (check_fit_r_and_b(tcm, w, h, xx, yy)) {
					P3("found shoulder: %d,%d", xx, yy);
					found_x = xx;
					found_y = yy;
					/* Insert this candidate, it is just a
						co-ordinate, reusing Area */
					done = update_candidate(tcm, xx, yy, w,
						h, field, CR_L2R_T2B, &best);
					if (done)
						break;
#ifdef X_SCAN_LIMITER
					/* change upper x bound */
					end_x = xx - 1;
#endif
					break;
				}
			} else {
				/* Optimization required only for Non Aligned,
				Aligned anyways skip by 32/64 tiles at a time */
				if (stride == 1 &&
				    pvt->map[xx][yy]->is2d) {
					xx = pvt->map[xx][yy]->p1.x;
					P3("moving to: %d,%d", xx, yy);
				}
			}
		}
		/* if you find a free area shouldering the given scan area on
		   then we can break */
#ifdef Y_SCAN_LIMITER
		if (found_x == start_x)
			break;
#endif
	}

	if (!best.a.tcm)
		return -ENOSPC;

	assign(area, best.a.p0.x, best.a.p0.y, best.a.p1.x, best.a.p1.y);
	return 0;
}

#ifdef SCAN_BOTTOM_UP
/**
 * @description: raster scan left to right from bottom to top; find if there is
 * a free area to fit a given w x h inside the 'scan area'. If there is a free
 * area, then adds to maybes candidates, which later is sent for selection
 * as per pre-defined criteria.
 *
 * @input:'w x h' width and height of the allocation area.
 * 'stride' - 64/32/None for start address alignment
 * 'field' - area in which the scan operation should take place
 *
 * @return 0 on success, non-0 error value on failure. On success
 * the 'area' area contains TL and BR corners of the allocated area
 *
 */
static s32 scan_l2r_b2t(struct tcm *tcm, u16 w, u16 h, u16 stride,
			struct tcm_area *field, struct tcm_area *area)
{
	s32 xx = 0, yy = 0, done = 0;
	s16 start_x = -1, end_x = -1, start_y = -1, end_y = -1;
	s16 found_x = -1, found_y = -1;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	struct score best = {{0}, {0}, {0}, 0};

	PA(2, "scan_l2r_b2t:", field);

	start_x = field->p0.x;
	end_x = field->p1.x;
	start_y = field->p0.y;
	end_y = field->p1.y;

	/* check scan area co-ordinates */
	if (field->p1.x < field->p0.x ||
	    field->p0.y < field->p1.y)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (w > INCL_LEN(end_x, start_x) || h > INCL_LEN(start_y, end_y))
		return -ENOSPC;

	start_x = ALIGN(start_x, stride);

	/* check if allocation would still fit in scan area */
	if (w > INCL_LEN(end_x, start_x))
		return -ENOSPC;

	/* adjust end_x and start_y, as allocation would not fit beyond */
	end_x = end_x - w + 1; /* + 1 to be inclusive */
	start_y = start_y - h + 1;

	P2("ali=%d x=%d..%d y=%d..%d", stride, start_x, end_x, start_y, end_y);

	/*
	 * Start scanning: These scans are always inclusive ones  so if we are
	 * given a start x = 0 is a valid value  so if we have a end_x = 255,
	 * 255th element is also checked
	 */
	for (yy = start_y; yy >= end_y && !done; yy--) {
		for (xx = start_x; xx <= end_x; xx += stride) {
			/* if NOT occupied */
			if (!pvt->map[xx][yy]) {
				if (check_fit_r_and_b(tcm, w, h, xx, yy)) {
					P3("found shoulder: %d,%d", xx, yy);
					found_x = xx;
					found_y = yy;
					/* Insert this candidate, it is just a
					 co-ordinate, reusing Area */
					done = update_candidate(tcm, xx, yy, w,
						h, field, CR_L2R_B2T, &best));
					if (done)
						break;

#ifdef X_SCAN_LIMITER
					/* change upper x bound */
					end_x = xx - 1;
#endif
					break;
				}
			} else {
				/* Optimization required only for Non Aligned,
				Aligned anyways skip by 32/64 tiles at a time */
				if (stride == 1 &&
				    pvt->map[xx][yy]->is2d) {
					xx = pvt->map[xx][yy]->p1.x;
					P3("moving to: %d,%d", xx, yy);
				}
			}
		}

		/* if you find a free area shouldering the given scan area on
		   then we can break */
#ifdef Y_SCAN_LIMITER
		if (found_x == start_x)
			break;
#endif
	}

	if (!best.a.tcm)
		return -ENOSPC;

	assign(area, best.a.p0.x, best.a.p0.y, best.a.p1.x, best.a.p1.y);
	return 0;
}
#endif
/*
Note: In General the cordinates specified in the scan area area relevant to the
scan sweep directions. i.e A scan Area from Top Left Corner will have
p0.x <= p1.x and p0.y <= p1.y. Where as A scan Area from bottom Right Corner
will have p1.x <= p0.x and p1.y <= p0.y
*/

/**
 * @description: raster scan right to left from bottom to top; find if there are
 * continuous free pages(one slot is one page, continuity always from left to
 * right) inside the 'scan area'. If there are enough continous free pages,
 * then it returns the start and end Tile/page co-ordinates inside 'area'
 *
 * @input:'num_pages' required,
 * 'field' - area in which the scan operation should take place
 *
 * @return 0 on success, non-0 error value on failure. On success
 * the 'area' area contains start and end slot (inclusive).
 *
 */
static s32 scan_r2l_b2t_one_dim(struct tcm *tcm, u32 num_pages,
		 struct tcm_area *field, struct tcm_area *area)
{
	s32 fit = false;
	u16 x, y;
	u16 left_x, left_y, busy_x, busy_y;
	s32 ret = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	/* check scan area co-ordinates */
	if (field->p0.y < field->p1.y)
		return -EINVAL;

	PA(2, "scan_r2l_b2t_one_dim:", field);

	/* Note: Checking sanctity of scan area
	 * The reason for checking this that 1D allocations assume that the X
	 ranges the entire TilerSpace X ie ALL Columns
	 * The scan area can limit only the Y ie, Num of Rows for 1D allocation.
	 We also expect we could have only 1 row for 1D allocation
	 * i.e our field p0.y and p1.y may have a same value.
	 */

	/* only support full width 1d scan area */
	if (pvt->width != field->p0.x - field->p1.x + 1)
		return -EINVAL;

	/* check if allocation would fit in scan area */
	if (num_pages > pvt->width * INCL_LEN(field->p0.y, field->p1.y))
		return -ENOSPC;

	left_x = field->p0.x;
	left_y = field->p0.y;
	while (!ret) {
		x = left_x;
		y = left_y;

		if (!pvt->map[x][y]) {
			ret = move_left(tcm, x, y, num_pages - 1,
					&left_x, &left_y);
			if (ret)
				break; /* out of space */

			P3("moved left %d slots: %d,%d", num_pages - 1,
						left_x, left_y);
			fit = check_fit_r_one_dim(tcm, left_x, left_y,
				  num_pages, &busy_x, &busy_y);
			if (fit) {
				assign(area, left_x, left_y,
				       busy_x, busy_y);
				break;
			} else {
				/* no fit, continue at the busy slot */
				x = busy_x;
				y = busy_y;
			}
		}

		/* now the tile is occupied, skip busy region */
		if (pvt->map[x][y]->is2d) {
			busy_x = pvt->map[x][y]->p0.x;
			busy_y = y;
		} else {
			busy_x = pvt->map[x][y]->p0.x;
			busy_y = pvt->map[x][y]->p0.y;
		}
		x = busy_x;
		y = busy_y;

		P3("moving left from: %d,%d", x, y);
		ret = move_left(tcm, x, y, 1, &left_x, &left_y);
	}

	return fit ? 0 : -ENOSPC;
}

/**
 * @description:
 *
 *
 *
 *
 * @input:
 *
 *
 * @return 0 on success, non-0 error value on failure. On success
 */
static s32 scan_areas_and_find_fit(struct tcm *tcm, u16 w, u16 h, u16 stride,
			    struct tcm_area *area)
{
	s32 ret = 0;
	struct tcm_area field = {0};
	u16 boundary_x = 0, boundary_y = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	s32 need_scan = 2;

	if (stride > 1) {
		boundary_x = pvt->div_pt.x - 1;
		boundary_y = pvt->div_pt.y - 1;

		/* more intelligence here */
		if (w > pvt->div_pt.x) {
			boundary_x = pvt->width - 1;
			need_scan--;
		}
		if (h > pvt->div_pt.y) {
			boundary_y = pvt->height - 1;
			need_scan--;
		}

		assign(&field, 0, 0, boundary_x, boundary_y);
		ret = scan_l2r_t2b(tcm, w, h, stride, &field, area);
		if (ret != 0 && need_scan) {
			/* scan the entire container if nothing found */
			assign(&field, 0, 0, pvt->width - 1, pvt->height - 1);
			ret = scan_l2r_t2b(tcm, w, h, stride, &field, area);
		}
	} else if (stride == 1) {
		boundary_x = pvt->div_pt.x;
		boundary_y = pvt->div_pt.y - 1;

		/* more intelligence here */
		if (w > (pvt->width - pvt->div_pt.x)) {
			boundary_x = 0;
			need_scan--;
		}
		if (h > pvt->div_pt.y) {
			boundary_y = pvt->height - 1;
			need_scan--;
		}

		assign(&field, pvt->width - 1, 0, boundary_x, boundary_y);
		ret = scan_r2l_t2b(tcm, w, h, stride, &field, area);

		if (ret != 0 && need_scan) {
			/* scan the entire container if nothing found */
			assign(&field, pvt->width - 1, 0, 0,
			       pvt->height - 1);
			ret = scan_r2l_t2b(tcm, w, h, stride, &field,
					   area);
		}
	}

	/* 3/30/2010: moved aligned to left, and unaligned to right side. */
#if 0
	else if (stride == 1) {
		/* use 64-align area so we don't grow down and shrink 1D area */
		if (h > pvt->div_pt.y) {
			need_scan -= 2;
			assign(&field, 0, 0, pvt->width - 1, pvt->height - 1);
			ret = scan_l2r_t2b(tcm, w, h, stride, &field, area);
		} else {
			assign(&field, 0, pvt->div_pt.y - 1, pvt->width - 1, 0);
			/* scan up in 64 and 32 areas accross whole width */
			ret = scan_l2r_b2t(tcm, w, h, stride, &field, area);
		}

		if (ret != 0 && need_scan) {
			assign(&field, 0, 0, pvt->width - 1, pvt->height - 1);
			ret = scan_l2r_t2b(tcm,  w, h, stride, &field, area);
		}
	}
#endif
	return ret;
}

static s32 check_fit_r_and_b(struct tcm *tcm, u16 w, u16 h, u16 left_x,
			     u16 top_y)
{
	u16 xx = 0, yy = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	for (yy = top_y; yy < top_y + h; yy++) {
		for (xx = left_x; xx < left_x + w; xx++) {
			if (pvt->map[xx][yy])
				return false;
		}
	}
	return true;
}

static s32 check_fit_r_one_dim(struct tcm *tcm, u16 x, u16 y, u32 num_pages,
			u16 *busy_x, u16 *busy_y)
{
	s32 ret = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	s32 i = 0;
	*busy_x = x;
	*busy_y = y;

	P2("checking fit for %d pages from %d,%d", num_pages, x, y);
	while (i < num_pages) {
		if (pvt->map[x][y]) {
			/* go to the start of the blocking allocation
			   to avoid unecessary checking */
			if (pvt->map[x][y]->is2d) {
				*busy_x = pvt->map[x][y]->p0.x;
				*busy_y = y;
			} else {
				*busy_x = pvt->map[x][y]->p0.x;
				*busy_y = pvt->map[x][y]->p0.y;
			}
			/* TODO: Could also move left in case of 2D */
			P2("after busy slot at: %d,%d", *busy_x, *busy_y);
			return false;
		}

		i++;

		/* break here so busy_x, busy_y will be correct */
		if (i == num_pages)
			break;

		ret = move_right(tcm, x, y, 1, busy_x, busy_y);
		if (ret)
			return false;

		x = *busy_x;
		y = *busy_y;
	}

	return true;
}

static void fill_2d_area(struct tcm *tcm, struct tcm_area *area,
			struct tcm_area *parent)
{
	s32 x, y;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	PA(2, "fill 2d area", area);
	for (x = area->p0.x; x <= area->p1.x; ++x)
		for (y = area->p0.y; y <= area->p1.y; ++y)
			pvt->map[x][y] = parent;
}

/* area should be a valid area */
static void fill_1d_area(struct tcm *tcm, struct tcm_area *area,
			struct tcm_area *parent)
{
	u16 x = 0, y = 0;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	PA(2, "fill 1d area", area);
	x = area->p0.x;
	y = area->p0.y;

	while (!(x == area->p1.x && y == area->p1.y)) {
		pvt->map[x++][y] = parent;
		if (x == pvt->width) {
			x = 0;
			y++;
		}
	}
	/* set the last slot */
	pvt->map[x][y] = parent;
}

static s32 update_candidate(struct tcm *tcm, u16 x0, u16 y0, u16 w, u16 h,
			    struct tcm_area *field, s32 criteria,
			    struct score *best)
{
	struct score me;	/* score for area */

	/*
	 * If first found is enabled then we stop looking
	 * NOTE: For Horizontal bias we always give the first found, because our
	 * scan is Horizontal-raster-based and the first candidate will always
	 * have the Horizontal bias.
	 */
	bool first = criteria & (CR_FIRST_FOUND | CR_BIAS_HORIZONTAL);

	assign(&me.a, x0, y0, x0 + w - 1, y0 + h - 1);

	/* calculate score for current candidate */
	if (!first) {
		get_busy_neigh_stats(tcm, w, h, &me.a, &me.n);
		me.neighs = BOUNDARY(&me.n) + OCCUPIED(&me.n);
		get_nearness_factor(field, &me.a, &me.f);
	}

	/* the 1st candidate is always the best */
	if (!best->a.tcm)
		goto better;

	/*****  TEMP *****/
	if (first)
		return 0;

	/* see if this are is better than the best so far */

	/* neighbor check */
	if ((criteria & CR_MAX_NEIGHS) &&
		me.neighs > best->neighs)
		goto better;

	/* vertical bias check */
	if ((criteria & CR_BIAS_VERTICAL) &&
	/*
	 * NOTE: not checking if lengths are same, because that does not
	 * find new shoulders on the same row after a fit
	 */
		INCL_LEN_MOD(me.a.p0.y, field->p0.y) >
		INCL_LEN_MOD(best->a.p0.y, field->p0.y))
		goto better;

	/* diagonal balance check */
	if ((criteria & CR_DIAGONAL_BALANCE) &&
		best->neighs <= me.neighs &&
		(best->neighs < me.neighs ||
		 /* this implies that neighs and occupied match */
		 OCCUPIED(&best->n) < OCCUPIED(&me.n) ||
		 (OCCUPIED(&best->n) == OCCUPIED(&me.n) &&
		  /* check the nearness factor */
		  best->f.x + best->f.y > me.f.x + me.f.y)))
		goto better;

	/* not better, keep going */
	return 0;

better:
	/* save current area as best */
	memcpy(best, &me, sizeof(me));
	best->a.tcm = tcm;
	return first;
}

/* get the nearness factor of an area in a search field */
static void get_nearness_factor(struct tcm_area *field,
			struct tcm_area *area, struct nearness_factor *nf)
{
	/* For the following calculation we need worry of +/- sign, the
	relative distances take of this. Multiplied by 1000, there
	is no floating point arithmetic used in kernel */

	nf->x = (s32)(area->p0.x - field->p0.x) * 1000 /
		(field->p1.x - field->p0.x);
	nf->y = (s32)(area->p0.y - field->p0.y) * 1000 /
		(field->p1.y - field->p0.y);
}

/* Neighbours
 *
 *   |<-----T------>|
 *  _ _______________  _
 * L |     Ar       | R
 * _ |______________|_
 *   |<-----B------>|
 */
static s32 get_busy_neigh_stats(struct tcm *tcm, u16 width, u16 height,
			 struct tcm_area *top_left_corner,
			 struct neighbour_stats *neighbour_stat)
{
	s16 xx = 0, yy = 0;
	struct tcm_area left_edge;
	struct tcm_area right_edge;
	struct tcm_area top_edge;
	struct tcm_area bottom_edge;
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;

	if (neighbour_stat == NULL)
		return -EINVAL;

	if (width == 0 || height == 0)
		return -EINVAL;

	/* Clearing any exisiting values */
	memset(neighbour_stat, 0, sizeof(*neighbour_stat));

	/* Finding Top Edge */
	assign(&top_edge, top_left_corner->p0.x, top_left_corner->p0.y,
	       top_left_corner->p0.x + width - 1, top_left_corner->p0.y);

	/* Finding Bottom Edge */
	assign(&bottom_edge, top_left_corner->p0.x,
	       top_left_corner->p0.y+height - 1,
	       top_left_corner->p0.x + width - 1,
	       top_left_corner->p0.y + height - 1);

	/* Finding Left Edge */
	assign(&left_edge, top_left_corner->p0.x, top_left_corner->p0.y,
	       top_left_corner->p0.x, top_left_corner->p0.y + height - 1);

	/* Finding Right Edge */
	assign(&right_edge, top_left_corner->p0.x + width - 1,
		top_left_corner->p0.y,
		top_left_corner->p0.x + width - 1,
		top_left_corner->p0.y + height - 1);

	/* dump_area(&top_edge);
	dump_area(&right_edge);
	dump_area(&bottom_edge);
	dump_area(&left_edge);
	*/

	/* Parsing through top & bottom edge */
	for (xx = top_edge.p0.x; xx <= top_edge.p1.x; xx++) {
		if (top_edge.p0.y - 1 < 0)
			neighbour_stat->top_boundary++;
		else if (pvt->map[xx][top_edge.p0.y - 1])
			neighbour_stat->top_occupied++;

		if (bottom_edge.p0.y + 1 > pvt->height - 1)
			neighbour_stat->bottom_boundary++;
		else if (pvt->map[xx][bottom_edge.p0.y+1])
			neighbour_stat->bottom_occupied++;
	}

	/* Parsing throught left and right edge */
	for (yy = left_edge.p0.y; yy <= left_edge.p1.y; ++yy) {
		if (left_edge.p0.x - 1 < 0)
			neighbour_stat->left_boundary++;
		else if (pvt->map[left_edge.p0.x - 1][yy])
			neighbour_stat->left_occupied++;

		if (right_edge.p0.x + 1 > pvt->width - 1)
			neighbour_stat->right_boundary++;
		else if (pvt->map[right_edge.p0.x + 1][yy])
			neighbour_stat->right_occupied++;

	}

	return 0;
}

static s32 move_left(struct tcm *tcm, u16 x, u16 y, u32 num_pages,
		     u16 *xx, u16 *yy)
{
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	u32 pos = x + pvt->width * y;

	if (pos < num_pages)
		return -ENOSPC;

	pos -= num_pages;
	*xx = pos % pvt->width;
	*yy = pos / pvt->width;
	return 0;
}

static s32 move_right(struct tcm *tcm, u16 x, u16 y, u32 num_pages,
		      u16 *xx, u16 *yy)
{
	struct sita_pvt *pvt = (struct sita_pvt *)tcm->pvt;
	u32 pos = x + pvt->width * y;

	if (num_pages > pvt->width * pvt->height - pos)
		return -ENOSPC;

	pos += num_pages;
	*xx = pos % pvt->width;
	*yy = pos / pvt->width;
	return 0;
}
