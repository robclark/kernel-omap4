/*
 * _tcm_sita.h
 *
 * SImple Tiler Allocator (SiTA) private structures.
 *
 * Author: Ravi Ramachandra <r.ramachandra@ti.com>
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

#ifndef _TCM_SITA_H
#define _TCM_SITA_H

#include "../tcm.h"

/* length between two coordinates */
#define LEN(a, b) ((a) > (b) ? (a) - (b) + 1 : (b) - (a) + 1)

#define BOUNDARY(stat) ((stat)->top_edge + (stat)->bottom_edge + \
				(stat)->left_edge + (stat)->right_edge)
#define OCCUPIED(stat) ((stat)->top_busy + (stat)->bottom_busy + \
				(stat)->left_busy + (stat)->right_busy)

enum criteria {
	CR_MAX_NEIGHS		= 0x01,
	CR_FIRST_FOUND		= 0x10,
	CR_BIAS_HORIZONTAL	= 0x20,
	CR_BIAS_VERTICAL	= 0x40,
	CR_DIAGONAL_BALANCE	= 0x80
};

/* nearness to the beginning of the search field from 0 to 1000 */
struct nearness_factor {
	s32 x;
	s32 y;
};

/*
 * Statistics on neighboring cells in each direction.  Edge is the container
 * boundary.  Busy refers to the number of naighbors that are occupied by a
 * tile.
 */
struct neighbor_stats {
	u16 left_edge;
	u16 left_busy;
	u16 top_edge;
	u16 top_busy;
	u16 right_edge;
	u16 right_busy;
	u16 bottom_edge;
	u16 bottom_busy;
};

/* structure to keep the score of a potential allocation */
struct score {
	struct nearness_factor	f;
	struct neighbor_stats	n;
	struct tcm_area		a;
	u16    neighs;		/* number of busy neighbors */
};

struct sita_pvt {
	u16 width;		/* container width */
	u16 height;		/* container height */
	struct mutex mtx;
	struct tcm_pt div_pt;	/* divider point splitting container */
	struct tcm_area ***map;	/* pointers to the parent area for each slot */
};

#endif
