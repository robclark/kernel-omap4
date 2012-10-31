/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * description of display timings
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_DISPLAY_TIMINGS_H
#define __LINUX_DISPLAY_TIMINGS_H

#include <linux/types.h>

struct timing_entry {
	u32 min;
	u32 typ;
	u32 max;
};

struct display_timing {
	struct timing_entry pixelclock;

	struct timing_entry hactive;
	struct timing_entry hfront_porch;
	struct timing_entry hback_porch;
	struct timing_entry hsync_len;

	struct timing_entry vactive;
	struct timing_entry vfront_porch;
	struct timing_entry vback_porch;
	struct timing_entry vsync_len;

	unsigned int vsync_pol_active;
	unsigned int hsync_pol_active;
	unsigned int de_pol_active;
	unsigned int pixelclk_pol;
	bool interlaced;
	bool doublescan;
};

struct display_timings {
	unsigned int num_timings;
	unsigned int native_mode;

	struct display_timing **timings;
};

/* placeholder function until ranges are really needed */
static inline u32 display_timing_get_value(struct timing_entry *te, int index)
{
	return te->typ;
}

static inline struct display_timing *display_timings_get(struct display_timings *disp,
							 int index)
{
	struct display_timing *dt;

	if (disp->num_timings > index) {
		dt = disp->timings[index];
		return dt;
	} else
		return NULL;
}

void timings_release(struct display_timings *disp);
void display_timings_release(struct display_timings *disp);

#endif
