/*
 * temperature.c -- CPU temprature monitoring
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
 * Written by Soós Péter <sp@osb.hu>, 2002-2004
 * Modified by Mathieu Bérard <mathieu.berard@crans.org>, 2006
 */

#include "omnibook.h"
#include "hardware.h"

static int omnibook_temperature_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	int retval;
	u8 temp;

	if ((retval = backend_byte_read(io_op, &temp)))
		return retval;

	len += sprintf(buffer + len, "CPU temperature:            %2d C\n", temp);

	return len;
}

static struct omnibook_tbl temp_table[] __initdata = {
	{XE3GF | TSP10 | TSM70 | TSM30X | TSX205, SIMPLE_BYTE(EC, XE3GF_CTMP, 0)},
	{XE3GC | AMILOD, SIMPLE_BYTE(EC, XE3GC_CTMP, 0)},
	{OB500 | OB510 | OB6000 | OB6100 | XE4500 | XE2, SIMPLE_BYTE(EC, OB500_CTMP, 0)},
	{OB4150, SIMPLE_BYTE(EC, OB4150_TMP, 0)},
	{0,}
};

static struct omnibook_feature __declared_feature temperature_driver = {
	.name = "temperature",
	.enabled = 1,
	.read = omnibook_temperature_read,
	.ectypes =
	    XE3GF | XE3GC | OB500 | OB510 | OB6000 | OB6100 | XE4500 | OB4150 | XE2 | AMILOD | TSP10
	    | TSM70 | TSM30X | TSX205,
	.tbl = temp_table,
};

module_param_named(temperature, temperature_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(temperature, "Use 0 to disable, 1 to enable thermal status and policy support");
/* End of file */
