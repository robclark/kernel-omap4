/*
 * xvmalloc.h
 *
 * Copyright (C) 2008, 2009  Nitin Gupta
 *
 * This code is released using a dual license strategy: GPL/LGPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of GNU General Public License Version 2.0
 * Released under the terms of GNU Lesser General Public License Version 2.1
 */

#ifndef _XVMALLOC_H_
#define _XVMALLOC_H_

#include <linux/types.h>

struct xv_pool;

struct xv_pool *xv_create_pool(void);
void xv_destroy_pool(struct xv_pool *pool);

int xv_malloc(struct xv_pool *pool, u32 size, u32 *pagenum, u32 *offset,
							gfp_t flags);
void xv_free(struct xv_pool *pool, u32 pagenum, u32 offset);

u32 xv_get_object_size(void *obj);
u64 xv_get_total_size_bytes(struct xv_pool *pool);

#endif
