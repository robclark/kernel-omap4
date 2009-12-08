/*
 * hotkeys.c -- code to handling Hotkey/E-Key/EasyAccess buttons
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

/* Predefined convinient on/off states */
#define HKEY_ON  HKEY_ONETOUCH|HKEY_MULTIMEDIA|HKEY_FN|HKEY_DOCK|HKEY_FNF5
#define HKEY_OFF 0

/*
 * Set hotkeys status and update recorded saved state
 */
static int hotkeys_set_save(struct omnibook_operation *io_op, unsigned int state)
{
	int retval;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;

	retval = __backend_hotkeys_set(io_op, state);
	if (retval < 0)
		goto out;

	/* Update saved state */
	io_op->backend->hotkeys_state = state & io_op->backend->hotkeys_write_cap;

	out:
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

/*
 * Read hotkeys status, fallback to reading saved state if real probing is not
 * supported.
 */
static int hotkeys_get_save(struct omnibook_operation *io_op, unsigned int *state)
{
	unsigned int read_state = 0;
	int retval = 0;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;

	if (io_op->backend->hotkeys_get)
		retval = __backend_hotkeys_get(io_op, &read_state);
	if (retval < 0)
		goto out;

	/* Return previously set state for the fields that are write only */
	*state = (read_state & io_op->backend->hotkeys_read_cap) + 
		 (io_op->backend->hotkeys_state & ~io_op->backend->hotkeys_read_cap);

	out:
	mutex_unlock(&io_op->backend->mutex);
	return 0;
}

/*
 * Power management handlers
 */

/*
 * Restore previously saved state
 */
static int omnibook_hotkeys_resume(struct omnibook_operation *io_op)
{
	int retval;
	mutex_lock(&io_op->backend->mutex);
	retval = __backend_hotkeys_set(io_op, io_op->backend->hotkeys_state);
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

/*
 * Disable hotkeys upon suspend (FIXME is the disabling required ?)
 */
static int omnibook_hotkeys_suspend(struct omnibook_operation *io_op)
{
	int retval = 0;
	retval = backend_hotkeys_set(io_op, HKEY_OFF);
	return retval;
}

static const char pretty_name[][27] = {
	"Onetouch buttons are",
	"Multimedia hotkeys are",
	"Fn hotkeys are",
	"Stick key is",
	"Press Fn twice to lock is",
	"Dock events are",
	"Fn + F5 hotkey is",
};

static int omnibook_hotkeys_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	int retval;
	unsigned int read_state = 0; /* buggy gcc 4.1 warning fix */
	unsigned int shift, mask;

	retval = hotkeys_get_save(io_op, &read_state);

	if (retval < 0)
		return retval;

	for (shift = 0; shift <= HKEY_LAST_SHIFT ; shift++) {
		mask = 1 << shift;
		/* we assume write capability or read capability imply support */
		if ((io_op->backend->hotkeys_read_cap | io_op->backend->hotkeys_write_cap) & mask)
			len +=
			    sprintf(buffer + len, "%s %s.\n", pretty_name[shift],
				    (read_state & mask) ? "enabled" : "disabled");
	}

	return len;
}

static int omnibook_hotkeys_write(char *buffer, struct omnibook_operation *io_op)
{
	unsigned int state;
	char *endp;

	if (strncmp(buffer, "off", 3) == 0)
		hotkeys_set_save(io_op, HKEY_OFF);
	else if (strncmp(buffer, "on", 2) == 0)
		hotkeys_set_save(io_op, HKEY_ON);
	else {
		state = simple_strtoul(buffer, &endp, 16);
		if (endp == buffer)
			return -EINVAL;
		else
			hotkeys_set_save(io_op, state);
	}
	return 0;
}

static int __init omnibook_hotkeys_init(struct omnibook_operation *io_op)
{
	int retval;

	printk(O_INFO "Enabling all hotkeys.\n");
	retval = hotkeys_set_save(io_op, HKEY_ON);
	return retval < 0 ? retval : 0;
}

static void __exit omnibook_hotkeys_cleanup(struct omnibook_operation *io_op)
{
	printk(O_INFO "Disabling all hotkeys.\n");
	hotkeys_set_save(io_op, HKEY_OFF);
}

static struct omnibook_tbl hotkeys_table[] __initdata = {
	{XE3GF | XE3GC | OB500 | OB510 | OB6000 | OB6100 | XE4500 | AMILOD | TSP10 | TSM30X, 
	COMMAND(KBC,OMNIBOOK_KBC_CMD_ONETOUCH_ENABLE,OMNIBOOK_KBC_CMD_ONETOUCH_DISABLE)},
	{TSM70, {CDI,}},
	{TSM40, {SMI,}},
	{TSX205, {ACPI,}},
	{0,}
};

static struct omnibook_feature __declared_feature hotkeys_driver = {
	.name = "hotkeys",
	.enabled = 1,
	.read = omnibook_hotkeys_read,
	.write = omnibook_hotkeys_write,
	.init = omnibook_hotkeys_init,
	.exit = omnibook_hotkeys_cleanup,
	.suspend = omnibook_hotkeys_suspend,
	.resume = omnibook_hotkeys_resume,
	.ectypes =
	    XE3GF | XE3GC | OB500 | OB510 | OB6000 | OB6100 | XE4500 | AMILOD | TSP10 | TSM70 | TSM30X |
	    TSM40 | TSX205,
	.tbl = hotkeys_table,
};

module_param_named(hotkeys, hotkeys_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(hotkeys, "Use 0 to disable, 1 to enable hotkeys handling");
/* End of file */
