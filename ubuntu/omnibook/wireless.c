/*
 * wireless.c Wifi feature
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Written by Mathieu Bérard <mathieu.berard@crans.org>, 2006
 *
 */

#include "omnibook.h"
#include "hardware.h"

static int omnibook_wifi_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	int retval;
	unsigned int state;

	if ((retval = backend_aerial_get(io_op, &state)))
		return retval;

	len +=
	    sprintf(buffer + len, "Wifi adapter is %s", (state & WIFI_EX) ? "present" : "absent");
	if (state & WIFI_EX)
		len +=
		    sprintf(buffer + len, " and %s", (state & WIFI_STA) ? "enabled" : "disabled");
	len += sprintf(buffer + len, ".\n");
	len +=
	    sprintf(buffer + len, "Wifi Kill switch is %s.\n", (state & KILLSWITCH) ? "on" : "off");

	return len;

}

static int omnibook_wifi_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval = 0;
	unsigned int state;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;	

	if ((retval = __backend_aerial_get(io_op, &state)))
		goto out;

	if (*buffer == '0')
		state &= ~WIFI_STA;
	else if (*buffer == '1')
		state |= WIFI_STA;
	else {
		retval = -EINVAL;
		goto out;
	}

	if ((retval = __backend_aerial_set(io_op, state)))
		return retval;

	out:		
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

static struct omnibook_feature wifi_driver;

static int __init omnibook_wifi_init(struct omnibook_operation *io_op)
{
	int retval = 0;
	unsigned int state;

/*
 *  Refuse enabling/disabling a non-existent device
 */

	if ((retval = backend_aerial_get(io_op, &state)))
		return retval;

	if (!(state & WIFI_EX))
		wifi_driver.write = NULL;

	return retval;
}

static struct omnibook_tbl wireless_table[] __initdata = {
	{TSM70 | TSX205, {ACPI,}},	/* stubs to select backend */
	{TSM40, {SMI,}},		/* stubs to select backend */
	{0,}
};

static struct omnibook_feature __declared_feature wifi_driver = {
	.name = "wifi",
	.enabled = 1,
	.read = omnibook_wifi_read,
	.write = omnibook_wifi_write,
	.init = omnibook_wifi_init,
	.ectypes = TSM70 | TSM40 | TSX205,
	.tbl = wireless_table,
};

module_param_named(wifi, wifi_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(wifi, "Use 0 to disable, 1 to enable Wifi adapter control");
