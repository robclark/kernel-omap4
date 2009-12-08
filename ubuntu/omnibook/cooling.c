/*
 * colling.c -- cooling methods feature
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

static int omnibook_cooling_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;

	len += sprintf(buffer + len, "Cooling method : %s\n", 
			io_op->backend->cooling_state ? "Performance"  : "Powersave" );

	mutex_unlock(&io_op->backend->mutex);
	return len;
}

static int omnibook_cooling_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval = 0;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;


	if (*buffer == '0') {
		retval = __backend_byte_write(io_op, 
				TSM70_COOLING_OFFSET + TSM70_COOLING_POWERSAVE);
	} else if (*buffer == '1') {
		retval = __backend_byte_write(io_op,
				TSM70_COOLING_OFFSET + TSM70_COOLING_PERF);
	} else {
		retval = -EINVAL;
		goto out;
	}

	/* *buffer is either '0' or '1' here */
	if (!retval)
		io_op->backend->cooling_state = *buffer - '0' ;

	mutex_unlock(&io_op->backend->mutex);

	out:
	return retval;
}

static int __init omnibook_cooling_init(struct omnibook_operation *io_op)
{
	mutex_lock(&io_op->backend->mutex);
	/* XXX: Assumed default cooling method: performance */
	io_op->backend->cooling_state = TSM70_COOLING_PERF;
	mutex_unlock(&io_op->backend->mutex);
	return 0;
}

static void __exit omnibook_cooling_exit(struct omnibook_operation *io_op)
{
	/* Set back cooling method to performance */	
	backend_byte_write(io_op, TSM70_COOLING_OFFSET + TSM70_COOLING_PERF);
}

static struct omnibook_tbl cooling_table[] __initdata = {
	{TSM70 | TSX205, {CDI, 0, TSM70_FN_INDEX, 0, 0, 0 }},
	{0,}
};

struct omnibook_feature __declared_feature cooling_driver = {
	.name = "cooling",
	.enabled = 1,
	.read = omnibook_cooling_read,
	.write = omnibook_cooling_write,
	.init = omnibook_cooling_init,
	.exit = omnibook_cooling_exit,
	.ectypes = TSM70 | TSX205,
	.tbl = cooling_table,
};

module_param_named(cooling, cooling_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(cooling, "Use 0 to disable, 1 to enable CPU cooling method control");

/* End of file */
