/*
 * Copyright 2012 Steffen Trumtrar <s.trumtrar@pengutronix.de>
 *
 * display timings of helpers
 *
 * This file is released under the GPLv2
 */

#ifndef __LINUX_OF_DISPLAY_TIMINGS_H
#define __LINUX_OF_DISPLAY_TIMINGS_H

#include <linux/display_timing.h>

#define OF_USE_NATIVE_MODE -1

struct display_timings *of_get_display_timing_list(struct device_node *np);
struct display_timing *of_get_display_timing(struct device_node *np);
int of_display_timings_exists(struct device_node *np);

#endif
