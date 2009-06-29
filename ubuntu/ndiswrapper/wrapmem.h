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

#ifndef _WRAPMEM_H_

/* set ALLOC_DEBUG to 1 to get information about memory used by both
 * ndiswrapper and Windows driver by reading
 * /proc/net/ndiswrapper/debug; this will also show memory leaks
 * (memory allocated but not freed) when ndiswrapper module is
 * unloaded.

 * ALLOC_DEBUG=2: details about individual allocations leaking is printed
 * ALLOC_DEBUG=3: tags in ExAllocatePoolWithTag leaking printed
*/

//#ifndef ALLOC_DEBUG
//#define ALLOC_DEBUG 1
//#endif

enum alloc_type { ALLOC_TYPE_KMALLOC_ATOMIC, ALLOC_TYPE_KMALLOC_NON_ATOMIC,
		  ALLOC_TYPE_VMALLOC_ATOMIC, ALLOC_TYPE_VMALLOC_NON_ATOMIC,
		  ALLOC_TYPE_SLACK, ALLOC_TYPE_PAGES, ALLOC_TYPE_MAX };

int wrapmem_init(void);
void wrapmem_exit(void);
void *slack_kmalloc(size_t size);
void slack_kfree(void *ptr);
void wrapmem_info(void);

#ifdef ALLOC_DEBUG
void *wrap_kmalloc(size_t size, gfp_t flags, const char *file, int line);
void *wrap_kzalloc(size_t size, gfp_t flags, const char *file, int line);
void wrap_kfree(void *ptr);
void *wrap_vmalloc(unsigned long size, const char *file, int line);
void *wrap__vmalloc(unsigned long size, gfp_t flags, pgprot_t prot,
		    const char *file, int line);
void wrap_vfree(void *ptr);
void *wrap_alloc_pages(gfp_t flags, unsigned int size,
		       const char *file, int line);
void wrap_free_pages(unsigned long ptr, int order);
int alloc_size(enum alloc_type type);

#ifndef _WRAPMEM_C_
#undef kmalloc
#undef kzalloc
#undef kfree
#undef vmalloc
#undef __vmalloc
#undef vfree
#define kmalloc(size, flags)				\
	wrap_kmalloc(size, flags, __FILE__, __LINE__)
#define kzalloc(size, flags)				\
	wrap_kzalloc(size, flags, __FILE__, __LINE__)
#define vmalloc(size)				\
	wrap_vmalloc(size, __FILE__, __LINE__)
#define __vmalloc(size, flags, prot)				\
	wrap__vmalloc(size, flags, prot, __FILE__, __LINE__)
#define kfree(ptr) wrap_kfree(ptr)
#define vfree(ptr) wrap_vfree(ptr)

#define wrap_get_free_pages(flags, size)			\
	wrap_alloc_pages(flags, size, __FILE__, __LINE__)
#undef free_pages
#define free_pages(ptr, order) wrap_free_pages(ptr, order)

#if ALLOC_DEBUG > 1
void *wrap_ExAllocatePoolWithTag(enum pool_type pool_type, SIZE_T size,
				 ULONG tag, const char *file, int line);
#define ExAllocatePoolWithTag(pool_type, size, tag)			\
	wrap_ExAllocatePoolWithTag(pool_type, size, tag, __FILE__, __LINE__)
#endif

#endif // _WRAPMEM_C_

#else

#define wrap_get_free_pages(flags, size)			\
	(void *)__get_free_pages(flags, get_order(size))

#endif // ALLOC_DEBUG

#endif
