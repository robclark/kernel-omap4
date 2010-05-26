/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include <linux/ctype.h>
#include "iscsi.h"

#ifndef __IOTYPE_H__
#define __IOTYPE_H__

struct iotype {
	const char *name;
	struct list_head iot_list;

	int (*attach)(struct iet_volume *dev, char *args);
	int (*make_request)(struct iet_volume *dev, struct tio *tio, int rw);
	int (*sync)(struct iet_volume *dev, struct tio *tio);
	void (*detach)(struct iet_volume *dev);
	void (*show)(struct iet_volume *dev, struct seq_file *seq);
};

extern struct iotype fileio;
extern struct iotype nullio;
extern struct iotype blockio;

extern int iotype_init(void);
extern void iotype_exit(void);

/* For option parameter parsing.
 * This is slightly iet specific: we only tolower() up to the first '='.
 * Note that this changes *c _in place_, but our parsing
 * routines copy the input to a scratch page before parsing anyways. */
static inline void iet_strtolower(char *c)
{
	if (!c)
		return;
	for (; *c && *c != '='; c++)
		*c = tolower(*c);
}

#endif
