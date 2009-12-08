/*
 * ec.c -- low level functions to access Embedded Controller,
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

/*
 * Interrupt control
 */

static DEFINE_SPINLOCK(omnibook_ec_lock);

/*
 * Registers of the embedded controller
 */

#define OMNIBOOK_EC_DATA		0x62
#define OMNIBOOK_EC_SC			0x66

/*
 * Embedded controller status register bits
 */

#define OMNIBOOK_EC_STAT_OBF		0x01	/* Output buffer full */
#define OMNIBOOK_EC_STAT_IBF		0x02	/* Input buffer full */


/*
 * Embedded controller commands
 */

#define OMNIBOOK_EC_CMD_READ		0x80
#define OMNIBOOK_EC_CMD_WRITE		0x81

/*
 * Wait for embedded controller buffer
 */

static int omnibook_ec_wait(u8 event)
{
	int timeout = OMNIBOOK_TIMEOUT;

	switch (event) {
	case OMNIBOOK_EC_STAT_OBF:
		while (!(inb(OMNIBOOK_EC_SC) & event) && timeout--)
			mdelay(1);
		break;
	case OMNIBOOK_EC_STAT_IBF:
		while ((inb(OMNIBOOK_EC_SC) & event) && timeout--)
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
 * Read from the embedded controller
 * Decide at run-time if we can use the much cleaner ACPI EC driver instead of
 * this implementation, this is the case if ACPI has been compiled and is not
 * disabled.
 */

static int omnibook_ec_read(const struct omnibook_operation *io_op, u8 * data)
{
	int retval;

#ifdef CONFIG_ACPI_EC
	if (likely(!acpi_disabled)) {
		retval = ec_read((u8) io_op->read_addr, data);
		if (io_op->read_mask)
			*data &= io_op->read_mask;
//		dprintk("ACPI EC read at %lx success %i.\n", io_op->read_addr, retval);
		return retval;
	}
#endif
	spin_lock_irq(&omnibook_ec_lock);
	retval = omnibook_ec_wait(OMNIBOOK_EC_STAT_IBF);
	if (retval)
		goto end;
	outb(OMNIBOOK_EC_CMD_READ, OMNIBOOK_EC_SC);
	retval = omnibook_ec_wait(OMNIBOOK_EC_STAT_IBF);
	if (retval)
		goto end;
	outb((u8) io_op->read_addr, OMNIBOOK_EC_DATA);
	retval = omnibook_ec_wait(OMNIBOOK_EC_STAT_OBF);
	if (retval)
		goto end;
	*data = inb(OMNIBOOK_EC_DATA);
	if (io_op->read_mask)
		*data &= io_op->read_mask;
      end:
	spin_unlock_irq(&omnibook_ec_lock);
//	dprintk("Custom EC read at %lx success %i.\n", io_op->read_addr, retval);
	return retval;
}

/*
 * Write to the embedded controller:
 * If OMNIBOOK_LEGACY is set, decide at run-time if we can use the much cleaner 
 * ACPI EC driver instead of this legacy implementation. 
 * This is the case if ACPI has been compiled and is not
 * disabled.
 * If OMNIBOOK_LEGACY is unset, we drop our custoim implementation
 */

static int omnibook_ec_write(const struct omnibook_operation *io_op, u8 data)
{
	int retval;

#ifdef CONFIG_ACPI_EC
	if (likely(!acpi_disabled)) {
		retval = ec_write((u8) io_op->write_addr, data);
//		dprintk("ACPI EC write at %lx success %i.\n", io_op->write_addr, retval);
		return retval;
	}
#endif

	spin_lock_irq(&omnibook_ec_lock);
	retval = omnibook_ec_wait(OMNIBOOK_EC_STAT_IBF);
	if (retval)
		goto end;
	outb(OMNIBOOK_EC_CMD_WRITE, OMNIBOOK_EC_SC);
	retval = omnibook_ec_wait(OMNIBOOK_EC_STAT_IBF);
	if (retval)
		goto end;
	outb((u8) io_op->write_addr, OMNIBOOK_EC_DATA);
	retval = omnibook_ec_wait(OMNIBOOK_EC_STAT_IBF);
	if (retval)
		goto end;
	outb(data, OMNIBOOK_EC_DATA);
      end:
	spin_unlock_irq(&omnibook_ec_lock);
//	dprintk("Custom EC write at %lx success %i.\n", io_op->write_addr, retval);
	return retval;
}

static int omnibook_ec_display(const struct omnibook_operation *io_op, unsigned int *state)
{
	int retval;
	u8 raw_state;

	retval = __backend_byte_read(io_op, &raw_state);
	if (retval < 0)
		return retval;

	*state = !!(raw_state) & DISPLAY_CRT_DET;

	return DISPLAY_CRT_DET;
}

/*
 * Backend interface declarations
 */

struct omnibook_backend ec_backend = {
	.name = "ec",
	.byte_read = omnibook_ec_read,
	.byte_write = omnibook_ec_write,
	.display_get = omnibook_ec_display,
};

/* End of file */
