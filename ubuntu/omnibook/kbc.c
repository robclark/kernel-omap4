/*
 * kbc.c -- low level functions to access Keyboard Controller
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

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include "hardware.h"

extern int omnibook_key_polling_enable(void);
extern int omnibook_key_polling_disable(void);

/*
 * Registers of the keyboard controller
 */

#define OMNIBOOK_KBC_DATA		0x60
#define OMNIBOOK_KBC_SC			0x64

/*
 * Keyboard controller status register bits
 */

#define OMNIBOOK_KBC_STAT_OBF		0x01	/* Output buffer full */
#define OMNIBOOK_KBC_STAT_IBF		0x02	/* Input buffer full */


/*
 * Interrupt control
 */

static DEFINE_SPINLOCK(omnibook_kbc_lock);

/*
 * Wait for keyboard buffer
 */

static int omnibook_kbc_wait(u8 event)
{
	int timeout = OMNIBOOK_TIMEOUT;

	switch (event) {
	case OMNIBOOK_KBC_STAT_OBF:
		while (!(inb(OMNIBOOK_KBC_SC) & event) && timeout--)
			mdelay(1);
		break;
	case OMNIBOOK_KBC_STAT_IBF:
		while ((inb(OMNIBOOK_KBC_SC) & event) && timeout--)
			mdelay(1);
		break;
	default:
		return -EINVAL;
	}
	if (timeout > 0)
		return 0;
	return -ETIME;
}

/*
 * Write to the keyboard command register
 */

static int omnibook_kbc_write_command(u8 cmd)
{
	int retval;

	spin_lock_irq(&omnibook_kbc_lock);
	retval = omnibook_kbc_wait(OMNIBOOK_KBC_STAT_IBF);
	if (retval)
		goto end;
	outb(cmd, OMNIBOOK_KBC_SC);
	retval = omnibook_kbc_wait(OMNIBOOK_KBC_STAT_IBF);
      end:
	spin_unlock_irq(&omnibook_kbc_lock);
	return retval;
}

/*
 * Write to the keyboard data register
 */

static int omnibook_kbc_write_data(u8 data)
{
	int retval;

	spin_lock_irq(&omnibook_kbc_lock);
	retval = omnibook_kbc_wait(OMNIBOOK_KBC_STAT_IBF);
	if (retval)
		goto end;
	outb(data, OMNIBOOK_KBC_DATA);
	retval = omnibook_kbc_wait(OMNIBOOK_KBC_STAT_IBF);
      end:
	spin_unlock_irq(&omnibook_kbc_lock);
	return retval;
}

/*
 * Send a command to keyboard controller
 */

static int omnibook_kbc_command(const struct omnibook_operation *io_op, u8 data)
{
	int retval;

	if ((retval = omnibook_kbc_write_command(OMNIBOOK_KBC_CONTROL_CMD)))
		return retval;

	retval = omnibook_kbc_write_data(data);
	return retval;
}

/*
 * Onetouch button hotkey handler
 */
static int omnibook_kbc_hotkeys(const struct omnibook_operation *io_op, unsigned int state)
{
	int retval;

	retval = __omnibook_toggle(io_op, !!(state & HKEY_ONETOUCH));
	return retval;
}

/*
 * Backend interface declarations
 */
struct omnibook_backend kbc_backend = {
	.name = "i8042",
	.hotkeys_write_cap = HKEY_ONETOUCH,
	.byte_write = omnibook_kbc_command,
	.hotkeys_set = omnibook_kbc_hotkeys,
};

/* End of file */
