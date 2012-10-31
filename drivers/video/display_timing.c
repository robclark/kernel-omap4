/*
 * generic display timing functions
 *
 * Copyright (c) 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>, Pengutronix
 *
 * This file is released under the GPLv2
 */

#include <linux/slab.h>
#include <linux/display_timing.h>

void timings_release(struct display_timings *disp)
{
	int i;

	for (i = 0; i < disp->num_timings; i++)
		kfree(disp->timings[i]);
}

void display_timings_release(struct display_timings *disp)
{
	timings_release(disp);
	kfree(disp->timings);
}
