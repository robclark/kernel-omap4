/*
 * lib.c -- Generic helpers functions
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
#include "compat.h"
#include <linux/input.h>

/*
 * Generic funtion for applying a mask on a value
 * Hack: degenerate to omnibook_toggle if there is no read method 
 * of if the read address is 0, this is used in blank.c 
 */
int __omnibook_apply_write_mask(const struct omnibook_operation *io_op, int toggle)
{
	int retval = 0;
	int mask;
	u8 data;

	if(!(io_op->backend->byte_read  && io_op->read_addr))
		return __omnibook_toggle(io_op,toggle);

	if ((retval = __backend_byte_read(io_op, &data)))
		return retval;

	if (toggle == 1)
		mask = io_op->on_mask;
	else if (toggle == 0)
		mask = io_op->off_mask;
	else
		return -EINVAL;

	if (mask > 0)
		data |= (u8) mask;
	else if (mask < 0)
		data &= ~((u8) (-mask));
	else
		return -EINVAL;

	retval = __backend_byte_write(io_op, data);

	return retval;
}

/*
 * Helper for toggle like operations
 */
int __omnibook_toggle(const struct omnibook_operation *io_op, int toggle)
{
	int retval;
	u8 data;

	data = toggle ? io_op->on_mask : io_op->off_mask;
	retval = __backend_byte_write(io_op, data);
	return retval;
}

void omnibook_report_key( struct input_dev *dev, unsigned int keycode)
{
	input_report_key(dev, keycode, 1);
	input_sync(dev);
	input_report_key(dev, keycode, 0);
	input_sync(dev);
}

/* End of file */
