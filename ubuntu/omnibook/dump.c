/*
 *  dump.c - Raw dump of EC register, stolen from ibm_acpi.c
 *
 *
 *  Copyright (C) 2004-2005 Borislav Deianov <borislav@users.sf.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "omnibook.h"
#include "hardware.h"

static u8 ecdump_regs[256];

static int ecdump_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	int i, j;
	u8 v;

	len +=
	    sprintf(buffer + len,
		    "EC      " " +00 +01 +02 +03 +04 +05 +06 +07"
		    " +08 +09 +0a +0b +0c +0d +0e +0f\n");

	if(mutex_lock_interruptible(&io_op->backend->mutex))
			return -ERESTARTSYS;

	for (i = 0; i < 255; i += 16) {
		len += sprintf(buffer + len, "EC 0x%02x:", i);
		for (j = 0; j < 16; j++) {
			io_op->read_addr = i +j;
			if (__backend_byte_read(io_op, &v))
				break;
			if (v != ecdump_regs[i + j])
				len += sprintf(buffer + len, " *%02x", v);
			else
				len += sprintf(buffer + len, "  %02x", v);
			ecdump_regs[i + j] = v;
		}
		len += sprintf(buffer + len, "\n");
		if (j != 16)
			break;
	}

	mutex_unlock(&io_op->backend->mutex);

	/* These are way too dangerous to advertise openly... */
#if 0
	len +=
	    sprintf(buffer + len,
		    "commands:\t0x<offset> 0x<value>" " (<offset> is 00-ff, <value> is 00-ff)\n");
	len +=
	    sprintf(buffer + len,
		    "commands:\t0x<offset> <value>  " " (<offset> is 00-ff, <value> is 0-255)\n");
#endif
	return len;
}

static int ecdump_write(char *buffer, struct omnibook_operation *io_op)
{

	int i, v;

	if (sscanf(buffer, "0x%x 0x%x", &i, &v) == 2) {
		/* i and v set */
	} else if (sscanf(buffer, "0x%x %u", &i, &v) == 2) {
		/* i and v set */
	} else
		return -EINVAL;
	if (i >= 0 && i < 256 && v >= 0 && v < 256) {
		io_op->write_addr = i;
		return backend_byte_write(io_op, v);
	} else
		return -EINVAL;

	return 0;
}

static struct omnibook_tbl dump_table[] __initdata = {
	{ALL_ECTYPES, {EC,}},
	{0,}
};

static struct omnibook_feature __declared_feature dump_driver = {
	.name = "dump",
	.enabled = 0,
	.read = ecdump_read,
	.write = ecdump_write,
	.tbl = dump_table,
};

module_param_named(dump, dump_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(dump, "Use 0 to disable, 1 to enable embedded controller register dump support");
/* End of file */
