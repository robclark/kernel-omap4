/*
 * dock.c -- docking station/port replicator support
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

static int omnibook_dock_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	u8 dock;
	int retval;

	if ((retval = backend_byte_read(io_op, &dock)))
		return retval;

	len += sprintf(buffer + len, "Laptop is %s\n", (dock) ? "docked" : "undocked");

	return len;
}

static int omnibook_dock_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval;

	switch (*buffer) {
	case '0':
		retval = backend_byte_write(io_op, 0);
		break;
	case '1':
		retval = backend_byte_write(io_op, 1);
		break;
	default:
		retval = -EINVAL;
	}

	return retval;
}

static struct omnibook_feature dock_driver;

static int __init omnibook_dock_init(struct omnibook_operation *io_op)
{
	/* writing is only supported on ectype 13 */
	if(!(omnibook_ectype & TSM40))
		dock_driver.write = NULL;
	
	return 0;
}

static struct omnibook_tbl dock_table[] __initdata = {
	{XE3GF, SIMPLE_BYTE(EC, XE3GF_CSPR, XE3GF_CSPR_MASK)},
	{OB500 | OB510 | OB6000 | OB6100, SIMPLE_BYTE(EC, OB500_STA1, OB500_DCKS_MASK)},
	{OB4150, SIMPLE_BYTE(EC, OB4150_DCID, 0)},
	{TSM40, {SMI, SMI_GET_DOCK, SMI_SET_DOCK, 0, 0, 0}},
	{0,}
};

static struct omnibook_feature __declared_feature dock_driver = {
	.name = "dock",
	.enabled = 0,
	.init = omnibook_dock_init,
	.read = omnibook_dock_read,
	.write = omnibook_dock_write,
	.ectypes = XE3GF | OB500 | OB510 | OB6000 | OB6100 | OB4150 | TSM40,
	.tbl = dock_table,
};

module_param_named(dock, dock_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(dock, "Use 0 to disable, 1 to enable docking station support");
/* End of file */
