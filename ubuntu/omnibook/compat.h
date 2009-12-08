/*
 * compat.h -- Older kernel (=> 2.6.11) support 
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Written by Mathieu Bérard <mathieu.berard@crans.org>, 2006
 */

#include <linux/version.h>

/*
 * For compatibility with kernel older than 2.6.16
 * Mutex to Semaphore fallback
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
#include <asm/semaphore.h>
#define DEFINE_MUTEX(lock)		DECLARE_MUTEX(lock)
#define	mutex_init(lock)		init_MUTEX(lock)
#define mutex_lock(lock)		down(lock)
#define mutex_lock_interruptible(lock)	down_interruptible(lock)
#define mutex_unlock(lock)		up(lock)
#define mutex_destroy(lock)		do { } while(0)
#else
#include <linux/mutex.h>
#endif

/*
 * For compatibility with kernel older than 2.6.14
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,14))
static void inline *kzalloc(size_t size, int flags)
{
	void *ret = kmalloc(size, flags);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
#endif

/*
 * For compatibility with kernel older than 2.6.11
 */

#ifndef DEFINE_SPINLOCK
#define DEFINE_SPINLOCK(s)              spinlock_t s = SPIN_LOCK_UNLOCKED
#endif

/*
 * Those kernel don't have ICH7 southbridge pcids
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11))
#define PCI_DEVICE_ID_INTEL_ICH7_0      0x27b8
#define PCI_DEVICE_ID_INTEL_ICH7_1      0x27b9
#define PCI_DEVICE_ID_INTEL_ICH7_30	0x27b0
#define PCI_DEVICE_ID_INTEL_ICH7_31     0x27bd
#endif



/* End of file */
