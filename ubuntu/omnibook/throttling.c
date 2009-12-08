/*
 * throttling.c --CPU throttling control feature
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
 * Written by Mathieu Bérard <mathieu.berard@crans.org>, 2007
 */

#include "omnibook.h"
#include "hardware.h"

/*
 * Throttling level/rate mapping found in ICH6M datasheets
 * the output is set to mimic the one of /proc/acpi/cpu/CPU0/throttling
 * XXX: We always assume that there are 8 T-States and one processor.
 */
static const int trate[8] = { 0, 12, 25, 37, 50, 62, 75, 87 };

static int omnibook_throttle_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	int tstate = 0;
	int retval, i;

	retval = backend_throttle_get(io_op, &tstate);
	if (retval < 0)
		return retval;

	len += sprintf(buffer + len, "state count:             8\n");
        len += sprintf(buffer + len, "active state:            T%d\n", tstate);
	for (i = 0; i < 8; i += 1)
	{
		len += sprintf(buffer + len, "   %cT%d:                  %02d%%\n",
			(i == tstate ? '*' : ' '),
			i,
			trate[i]);
	}

	return len;
}

static int omnibook_throttle_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval = 0;
	int data;
	char *endp;

	data = simple_strtoul(buffer, &endp, 10);
	if ((endp == buffer) || (data > 7)) /* There are 8 throttling levels */
		return -EINVAL;
	else
		retval = backend_throttle_set(io_op, data);
	
	return retval;
}


static struct omnibook_tbl throttle_table[] __initdata = {
	{TSM70 | TSX205, {ACPI,}},
	{0,}
};

struct omnibook_feature __declared_feature throttle_driver = {
	.name = "throttling",
	.enabled = 1,
	.read = omnibook_throttle_read,
	.write = omnibook_throttle_write,
	.ectypes = TSM70 | TSX205,
	.tbl = throttle_table,
};

module_param_named(throttle, throttle_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(throttle, "Use 0 to disable, 1 to enable CPU throttling control");

/* End of file */
