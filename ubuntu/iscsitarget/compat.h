/*
 * Kernel compatibility routines
 *
 * Copyright (C) 2008 Ross Walker <rswwalker at gmail dot com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef __IET_COMPAT_H__
#define __IET_COMPAT_H__

#include <linux/version.h>

#ifndef DECLARE_COMPLETION_ONSTACK
#define DECLARE_COMPLETION_ONSTACK(work) DECLARE_COMPLETION(work)
#endif

#ifndef is_power_of_2
#define is_power_of_2(n) (n != 0 && ((n & (n - 1)) == 0))
#endif

#ifndef log2
#define log2(n) ((sizeof(n) <= 4) ? (fls(n) - 1) : (fls64(n) - 1))
#endif

#ifndef roundup_pow_of_two
#define roundup_pow_of_two(n) (1UL << fls_long(n - 1))
#endif

#endif	/* __IET_COMPAT_H__ */
