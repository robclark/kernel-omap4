/*
 * pio.c -- low level functions I/O ports
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
 * IO port backend. Only support single or dual ports operations
 * private data structure: it's the linked list of requested ports
 * 
 * Race condition issue: omnibook_pio_init/exit functions are only called from
 * omnibook_backend_match and omnibook_remove from init.c, this should happen
 * only at module init/exit time so there is no need for a lock.
 */

struct pio_priv_data_t {
	unsigned long addr;
	struct kref refcount;
	struct list_head list;
};

static struct pio_priv_data_t pio_priv_data = {
	.addr = 0,
	.list = LIST_HEAD_INIT(pio_priv_data.list),
};

/*
 * Match an entry in the linked list helper function: see if we have and entry
 * whose addr field match maddr
 */
static struct pio_priv_data_t *omnibook_match_port(struct pio_priv_data_t *data,
						      unsigned long maddr)
{
	struct pio_priv_data_t *cursor;

	list_for_each_entry(cursor, &data->list, list) {
		if (cursor->addr == maddr) {
			return cursor;
		}
	}
	return NULL;
}

/*
 * See if we have to request raddr
 */
static int omnibook_claim_port(struct pio_priv_data_t *data, unsigned long raddr)
{
	struct pio_priv_data_t *match, *new;

	match = omnibook_match_port(data, raddr);
	if (match) {
		/* Already requested by us: increment kref and quit */
		kref_get(&match->refcount);
		return 0;
	}

	/* there was no match: request the region and add to list */
	if (!request_region(raddr, 1, OMNIBOOK_MODULE_NAME)) {
		printk(O_ERR "Request I/O port error\n");
		return -ENODEV;
	}

	new = kmalloc(sizeof(struct pio_priv_data_t), GFP_KERNEL);
	if (!new) {
		release_region(raddr, 1);
		return -ENOMEM;
	}

	kref_init(&new->refcount);
	new->addr = raddr;
	list_add(&new->list, &data->list);

	return 0;
}

/*
 * Register read_addr and write_addr
 */
static int omnibook_pio_init(const struct omnibook_operation *io_op)
{
	int retval = 0;

	if (io_op->read_addr
	    && (retval = omnibook_claim_port(io_op->backend->data, io_op->read_addr)))
		goto out;

	if (io_op->write_addr && (io_op->write_addr != io_op->read_addr))
		retval = omnibook_claim_port(io_op->backend->data, io_op->write_addr);

      out:
	return retval;
}

/*
 * REALLY release a port
 */
static void omnibook_free_port(struct kref *ref)
{
	struct pio_priv_data_t *data;

	data = container_of(ref, struct pio_priv_data_t, refcount);
	release_region(data->addr, 1);
	list_del(&data->list);
	kfree(data);
}

/*
 * Unregister read_addr and write_addr
 */
static void omnibook_pio_exit(const struct omnibook_operation *io_op)
{
	struct pio_priv_data_t *match;

	match = omnibook_match_port(io_op->backend->data, io_op->read_addr);
	if (match)
		kref_put(&match->refcount, omnibook_free_port);

	match = omnibook_match_port(io_op->backend->data, io_op->write_addr);
	if (match)
		kref_put(&match->refcount, omnibook_free_port);

}

static int omnibook_io_read(const struct omnibook_operation *io_op, u8 * value)
{
	*value = inb(io_op->read_addr);
	if (io_op->read_mask)
		*value &= io_op->read_mask;
	return 0;
}

static int omnibook_io_write(const struct omnibook_operation *io_op, u8 value)
{
	outb(io_op->write_addr, value);
	return 0;
}

/*
 * Backend interface declarations
 */
struct omnibook_backend pio_backend = {
	.name = "pio",
	.data = &pio_priv_data,
	.init = omnibook_pio_init,
	.exit = omnibook_pio_exit,
	.byte_read = omnibook_io_read,
	.byte_write = omnibook_io_write,
};

/* End of file */
