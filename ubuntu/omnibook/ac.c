/*
 * ac.c -- AC adapter related functions
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

static int omnibook_ac_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	u8 ac;
	int retval;

	retval = backend_byte_read(io_op, &ac);
	if (retval < 0)
		return retval;

	len += sprintf(buffer + len, "AC %s\n", (!!ac) ? "on-line" : "off-line");

	return len;
}

static struct omnibook_tbl ac_table[] __initdata = {
	{XE3GF | TSP10 | TSM30X | TSM70, SIMPLE_BYTE(EC, XE3GF_ADP, XE3GF_ADP_MASK)},
	{XE3GC | AMILOD, SIMPLE_BYTE(EC, XE3GC_STA1, XE3GC_ADP_MASK)},
	{OB500 | OB510 | OB6000 | OB6100 | XE4500, SIMPLE_BYTE(EC, OB500_STA2, OB500_ADP_MASK)},
	{OB4150, SIMPLE_BYTE(EC, OB4150_ADP, OB4150_ADP_MASK)},
	{XE2, SIMPLE_BYTE(EC, XE2_STA1, XE2_ADP_MASK)},
	{0,}
};

struct omnibook_feature __declared_feature ac_driver = {
	.name = "ac",
#ifdef CONFIG_OMNIBOOK_LEGACY
	.enabled = 1,
#else
	.enabled = 0,
#endif
	.read = omnibook_ac_read,
	.ectypes = XE3GF | XE3GC | OB500 | OB510 | OB6000 | OB6100 | XE4500 | OB4150 | XE2 | AMILOD | TSP10 | TSM70 | TSM30X,
	.tbl = ac_table,
};

module_param_named(ac, ac_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(ac, "Use 0 to disable, 1 to enable AC adapter status monitoring");

/* End of file */
