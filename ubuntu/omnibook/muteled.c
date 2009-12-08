/*
 * mutled.c -- MUTE LED control
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
 * Written by Thomas Perl <thp@perli.net>, 2006
 * Modified by Mathieu Bérard <mathieu.berard@crans.org>, 2006
 */

#include "omnibook.h"
#include "hardware.h"

static int omnibook_muteled_set(struct omnibook_operation *io_op, int status)
{
	int retval = 0;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;

	if((retval = __omnibook_toggle(io_op, !!status))) {
		printk(O_ERR "Failed muteled %s command.\n", status ? "on" : "off");
		goto out;
	}

	io_op->backend->muteled_state = !!status;

	out:
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

/*
 * Hardware query is unsupported, reading is unreliable.
 */
static int omnibook_muteled_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;
	len +=
	    sprintf(buffer + len, "Last mute LED action was an %s command.\n",
		    io_op->backend->touchpad_state ? "on" : "off");

	mutex_unlock(&io_op->backend->mutex);
	return len;
}

static int omnibook_muteled_write(char *buffer, struct omnibook_operation *io_op)
{
	int cmd;

	if (*buffer == '0' || *buffer == '1') {
		cmd = *buffer - '0';
		if (!omnibook_muteled_set(io_op, cmd)) {
			dprintk("Switching mute LED to %s state.\n", cmd ? "on" : "off");
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

/*
 * May re-enable muteled upon resume
 */
static int omnibook_muteled_resume(struct omnibook_operation *io_op)
{	
	int retval;
	mutex_lock(&io_op->backend->mutex);
	retval = __omnibook_toggle(io_op, !!io_op->backend->touchpad_state);
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

/*
 * Switch muteled off upon exit
 */
static void __exit omnibook_muteled_cleanup(struct omnibook_operation *io_op)
{
	omnibook_muteled_set(io_op, 0);
}

static struct omnibook_tbl muteled_table[] __initdata = {
	{XE4500, COMMAND(KBC, OMNIBOOK_KBC_CMD_MUTELED_ON, OMNIBOOK_KBC_CMD_MUTELED_OFF)},
	{0,}
};

static struct omnibook_feature __declared_feature muteled_driver = {
	.name = "muteled",
	.enabled = 1,
	.read = omnibook_muteled_read,
	.write = omnibook_muteled_write,
	.exit = omnibook_muteled_cleanup,
	.resume = omnibook_muteled_resume,
	.ectypes = XE4500,
	.tbl = muteled_table,
};

module_param_named(muteled, muteled_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(muteled, "Use 0 to disable, 1 to enable 'Audo Mute' LED control");
