/*
 *  Copyright (C) 2006 Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#define _WRAPMEM_C_

#include "ntoskernel.h"

struct slack_alloc_info {
	struct nt_list list;
	size_t size;
};

static struct nt_list allocs;
static struct nt_list slack_allocs;
static spinlock_t alloc_lock;

struct vmem_block {
	struct nt_list list;
	int size;
};

static struct nt_list vmem_list;

#if defined(ALLOC_DEBUG)
struct alloc_info {
	enum alloc_type type;
	size_t size;
#if ALLOC_DEBUG > 1
	struct nt_list list;
	const char *file;
	int line;
	ULONG tag;
#endif
};

static atomic_t alloc_sizes[ALLOC_TYPE_MAX];
#endif

void wrapmem_info(void)
{
#ifdef ALLOC_DEBUG
	enum alloc_type type;
	for (type = 0; type < ALLOC_TYPE_MAX; type++)
		INFO("total size of allocations in %d: %d",
		       type, atomic_read(&alloc_sizes[type]));
#endif
}

/* allocate memory and add it to list of allocated pointers; if a
 * driver doesn't free this memory for any reason (buggy driver or we
 * allocate space behind driver's back since we need more space than
 * corresponding Windows structure provides etc.), this gets freed
 * automatically when module is unloaded
 */
void *slack_kmalloc(size_t size)
{
	struct slack_alloc_info *info;
	gfp_t flags;

	ENTER4("size = %lu", (unsigned long)size);

	if (irql_gfp() & GFP_ATOMIC)
		flags = GFP_ATOMIC;
	else
		flags = GFP_KERNEL;
	info = kmalloc(size + sizeof(*info), flags);
	if (!info)
		return NULL;
	info->size = size;
	spin_lock_bh(&alloc_lock);
	InsertTailList(&slack_allocs, &info->list);
	spin_unlock_bh(&alloc_lock);
#ifdef ALLOC_DEBUG
	atomic_add(size, &alloc_sizes[ALLOC_TYPE_SLACK]);
#endif
	TRACE4("%p, %p", info, info + 1);
	EXIT4(return info + 1);
}

/* free pointer and remove from list of allocated pointers */
void slack_kfree(void *ptr)
{
	struct slack_alloc_info *info;

	ENTER4("%p", ptr);
	info = ptr - sizeof(*info);
	spin_lock_bh(&alloc_lock);
	RemoveEntryList(&info->list);
	spin_unlock_bh(&alloc_lock);
#ifdef ALLOC_DEBUG
	atomic_sub(info->size, &alloc_sizes[ALLOC_TYPE_SLACK]);
#endif
	kfree(info);
	EXIT4(return);
}

#if defined(ALLOC_DEBUG)
void *wrap_kmalloc(size_t size, gfp_t flags, const char *file, int line)
{
	struct alloc_info *info;

	info = kmalloc(size + sizeof(*info), flags);
	if (!info)
		return NULL;
	if (flags & GFP_ATOMIC)
		info->type = ALLOC_TYPE_KMALLOC_ATOMIC;
	else
		info->type = ALLOC_TYPE_KMALLOC_NON_ATOMIC;
	info->size = size;
	atomic_add(size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	info->file = file;
	info->line = line;
	info->tag = 0;
	spin_lock_bh(&alloc_lock);
	InsertTailList(&allocs, &info->list);
	spin_unlock_bh(&alloc_lock);
#endif
	TRACE4("%p", info + 1);
	return info + 1;
}

void *wrap_kzalloc(size_t size, gfp_t flags, const char *file, int line)
{
	void *ptr = wrap_kmalloc(size, flags, file, line);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

void wrap_kfree(void *ptr)
{
	struct alloc_info *info;

	TRACE4("%p", ptr);
	if (!ptr)
		return;
	info = ptr - sizeof(*info);
	atomic_sub(info->size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	spin_lock_bh(&alloc_lock);
	RemoveEntryList(&info->list);
	spin_unlock_bh(&alloc_lock);
	if (!(info->type == ALLOC_TYPE_KMALLOC_ATOMIC ||
	      info->type == ALLOC_TYPE_KMALLOC_NON_ATOMIC))
		WARNING("invliad type: %d", info->type);
#endif
	kfree(info);
}

void *wrap_vmalloc(unsigned long size, const char *file, int line)
{
	struct alloc_info *info;

	info = vmalloc(size + sizeof(*info));
	if (!info)
		return NULL;
	info->type = ALLOC_TYPE_VMALLOC_NON_ATOMIC;
	info->size = size;
	atomic_add(size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	info->file = file;
	info->line = line;
	info->tag = 0;
	spin_lock_bh(&alloc_lock);
	InsertTailList(&allocs, &info->list);
	spin_unlock_bh(&alloc_lock);
#endif
	return info + 1;
}

void *wrap__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot,
		    const char *file, int line)
{
	struct alloc_info *info;

	info = __vmalloc(size + sizeof(*info), gfp_mask, prot);
	if (!info)
		return NULL;
	if (gfp_mask & GFP_ATOMIC)
		info->type = ALLOC_TYPE_VMALLOC_ATOMIC;
	else
		info->type = ALLOC_TYPE_VMALLOC_NON_ATOMIC;
	info->size = size;
	atomic_add(size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	info->file = file;
	info->line = line;
	info->tag = 0;
	spin_lock_bh(&alloc_lock);
	InsertTailList(&allocs, &info->list);
	spin_unlock_bh(&alloc_lock);
#endif
	return info + 1;
}

void wrap_vfree(void *ptr)
{
	struct alloc_info *info;

	info = ptr - sizeof(*info);
	atomic_sub(info->size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	spin_lock_bh(&alloc_lock);
	RemoveEntryList(&info->list);
	spin_unlock_bh(&alloc_lock);
	if (!(info->type == ALLOC_TYPE_VMALLOC_ATOMIC ||
	      info->type == ALLOC_TYPE_VMALLOC_NON_ATOMIC))
		WARNING("invliad type: %d", info->type);
#endif
	vfree(info);
}

void *wrap_alloc_pages(gfp_t flags, unsigned int size,
		       const char *file, int line)
{
	struct alloc_info *info;

	size += sizeof(*info);
	info = (struct alloc_info *)__get_free_pages(flags, get_order(size));
	if (!info)
		return NULL;
	info->type = ALLOC_TYPE_PAGES;
	info->size = size;
	atomic_add(size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	info->file = file;
	info->line = line;
	info->tag = 0;
	spin_lock_bh(&alloc_lock);
	InsertTailList(&allocs, &info->list);
	spin_unlock_bh(&alloc_lock);
#endif
	return info + 1;
}

void wrap_free_pages(unsigned long ptr, int order)
{
	struct alloc_info *info;

	info = (void *)ptr - sizeof(*info);
	atomic_sub(info->size, &alloc_sizes[info->type]);
#if ALLOC_DEBUG > 1
	spin_lock_bh(&alloc_lock);
	RemoveEntryList(&info->list);
	spin_unlock_bh(&alloc_lock);
	if (info->type != ALLOC_TYPE_PAGES)
		WARNING("invliad type: %d", info->type);
#endif
	free_pages((unsigned long)info, get_order(info->size));
}

#if ALLOC_DEBUG > 1
#undef ExAllocatePoolWithTag
void *wrap_ExAllocatePoolWithTag(enum pool_type pool_type, SIZE_T size,
				 ULONG tag, const char *file, int line)
{
	void *addr;
	struct alloc_info *info;

	ENTER4("pool_type: %d, size: %lu, tag: %u", pool_type, size, tag);
	addr = ExAllocatePoolWithTag(pool_type, size, tag);
	if (!addr)
		return NULL;
	info = addr - sizeof(*info);
	info->file = file;
	info->line = line;
	info->tag = tag;
	EXIT4(return addr);
}
#endif

int alloc_size(enum alloc_type type)
{
	if (type >= 0 && type < ALLOC_TYPE_MAX)
		return atomic_read(&alloc_sizes[type]);
	else
		return -EINVAL;
}

#endif // ALLOC_DEBUG

int wrapmem_init(void)
{
	InitializeListHead(&allocs);
	InitializeListHead(&slack_allocs);
	InitializeListHead(&vmem_list);
	spin_lock_init(&alloc_lock);
	return 0;
}

void wrapmem_exit(void)
{
	enum alloc_type type;
	struct nt_list *ent;

	/* free all pointers on the slack list */
	while (1) {
		struct slack_alloc_info *info;
		spin_lock_bh(&alloc_lock);
		ent = RemoveHeadList(&slack_allocs);
		spin_unlock_bh(&alloc_lock);
		if (!ent)
			break;
		info = container_of(ent, struct slack_alloc_info, list);
#ifdef ALLOC_DEBUG
		atomic_sub(info->size, &alloc_sizes[ALLOC_TYPE_SLACK]);
#endif
		kfree(info);
	}
	type = 0;
#ifdef ALLOC_DEBUG
	for (type = 0; type < ALLOC_TYPE_MAX; type++) {
		int n = atomic_read(&alloc_sizes[type]);
		if (n)
			WARNING("%d bytes of memory in %d leaking", n, type);
	}

#if ALLOC_DEBUG > 1
	while (1) {
		struct alloc_info *info;

		spin_lock_bh(&alloc_lock);
		ent = RemoveHeadList(&allocs);
		spin_unlock_bh(&alloc_lock);
		if (!ent)
			break;
		info = container_of(ent, struct alloc_info, list);
		atomic_sub(info->size, &alloc_sizes[ALLOC_TYPE_SLACK]);
		WARNING("%p in %d of size %zu allocated at %s(%d) "
			"with tag 0x%08X leaking; freeing it now",
			info + 1, info->type, info->size, info->file,
			info->line, info->tag);
		if (info->type == ALLOC_TYPE_KMALLOC_ATOMIC ||
		    info->type == ALLOC_TYPE_KMALLOC_NON_ATOMIC)
			kfree(info);
		else if (info->type == ALLOC_TYPE_VMALLOC_ATOMIC ||
			 info->type == ALLOC_TYPE_VMALLOC_NON_ATOMIC)
			vfree(info);
		else if (info->type == ALLOC_TYPE_PAGES)
			free_pages((unsigned long)info, get_order(info->size));
		else
			WARNING("invalid type: %d; not freed", info->type);
	}
#endif
#endif
	return;
}
