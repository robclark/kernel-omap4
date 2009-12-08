/*
 * compal.c -- EC PIO Command/Data/Index mode low-level access code
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

#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kref.h>

#include <asm/io.h>
#include "hardware.h"

/*
 * ATI's IXP PCI-LPC bridge
 */
#define PCI_DEVICE_ID_ATI_SB400 0x4377

/* 
 * PCI Config space regiser
 * Laptop with Intel ICH Chipset
 * See ICH6M and ICH7M spec
 */
#define INTEL_LPC_GEN1_DEC	0x84
#define INTEL_LPC_GEN4_DEC	0x90
#define INTEL_IOPORT_BASE 	0xff2c

/* 
 * PCI Config space regiser
 * Laptop with ATI Chipset
 * FIXME Untested, name unknown
 */
#define ATI_LPC_REG		0x4a
#define ATI_IOPORT_BASE 	0xfd60

/* 
 *This interface uses 2 ports for command and 1 port for data
 *These are relative to the ioport_base address
 */

#define PIO_PORT_COMMAND1	0x1
#define PIO_PORT_COMMAND2	0x2
#define PIO_PORT_DATA		0x3

/*
 * Private data of this backend
 */
static struct pci_dev *lpc_bridge;	/* Southbridge chip ISA bridge/LPC interface PCI device */
static u32 ioport_base;		/* PIO base adress */
static union {
	u16 word;
	u32 dword;
} pci_reg_state;		/* Saved state of register in PCI config spave */

/*
 * Possible list of supported southbridges
 * Here mostly to implement a more or less clean PCI probing
 * Works only because of previous DMI probing.
 * Shared with nbsmi backend
 */
const struct pci_device_id lpc_bridge_table[] = {
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AA_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801AB_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801BA_10, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_12, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801DB_12, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801E_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801EB_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ESB_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH6_2, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_30, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_31, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
        {PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH8_4, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_SB400, PCI_ANY_ID, PCI_ANY_ID, 0, 0,},
	{0,},			/* End of list */
};

/*
 * Low-level Read function:
 * Write a 2-bytes wide command to the COMMAND ports
 * Read the result in the DATA port
 */
static unsigned char lowlevel_read(u16 command)
{
	unsigned char data;
	outb((command & 0xff00) >> 8, ioport_base + PIO_PORT_COMMAND1);
	outb(command & 0x00ff, ioport_base + PIO_PORT_COMMAND2);
	data = inb(ioport_base + PIO_PORT_DATA);
	return data;
}

/*
 * Low-level Write function:
 * Write a 2-bytes wide command to the COMMAND ports
 * Write the result in the DATA port
 */
static void lowlevel_write(u16 command, u8 data)
{
	outb((command & 0xff00) >> 8, ioport_base + PIO_PORT_COMMAND1);
	outb(command & 0x00ff, ioport_base + PIO_PORT_COMMAND2);
	outb(data, ioport_base + PIO_PORT_DATA);
}

/*
 * Probe for a state of the PIO Command/Data/Index interface
 * Give some time for the controler to settle in the desired state
 * mode significance:
 * 0: Waiting for command
 * 1,2,3: I am confused FIXME
 */
static int check_cdimode_flag(unsigned int mode)
{
	int i;
	int retval;

	/*dprintk("Index mode:");*/
	for (i = 1; i <= 250; i++) {
		retval = lowlevel_read(0xfbfc);
		/*dprintk_simple(" [%i]", retval);*/
		if (retval == mode) {
			/*dprintk_simple(".\n");
			dprintk("Index Mode Ok (%i) after %i iter\n", mode, i);*/
			return 0;
		}
		udelay(100);
	}
	printk(O_ERR "check_cdimode_flag timeout.\n");
	return -ETIME;
}

/*
 * Check for conventional default (0xf432) state in Commad ports
 */
static int check_default_state(void)
{
	int i;

	for (i = 1; i <= 250; i++) {
		if ((inb(ioport_base + PIO_PORT_COMMAND1) == 0xf4)
		    && (inb(ioport_base + PIO_PORT_COMMAND2) == 0x32))
			return 0;
		udelay(100);
	}
	printk(O_ERR "check_default_state timeout.\n");
	return -ETIME;
}

/*
 * Enable EC Command/Data/Index PIO Access and then check EC state.
 * Enabling is done in PCI config space of the LPC bridge.
 *
 * Just after Enabling, the EC should be in a precisly defined state:
 * - PIO should be in a conventional default state (0xf432 in the Command ports)
 * - Command/Data/Index interface waiting for command
 * The EC is expected to be in that state prior to any attempt to use the interface.
 *
 */
static int enable_cdimode(void)
{
	union {
		u16 word;
		u32 dword;
	} value;

	switch (lpc_bridge->vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (lpc_bridge->device) {
		case PCI_DEVICE_ID_INTEL_ICH7_0:	/* ICH7 */
		case PCI_DEVICE_ID_INTEL_ICH7_1:
		case PCI_DEVICE_ID_INTEL_ICH7_30:
		case PCI_DEVICE_ID_INTEL_ICH7_31:
		case PCI_DEVICE_ID_INTEL_ICH8_4:	/* ICH8 */
			pci_read_config_dword(lpc_bridge, INTEL_LPC_GEN4_DEC, &(value.dword));
			pci_reg_state.dword = value.dword;
			value.dword = 0x3CFF21;
			pci_write_config_dword(lpc_bridge, INTEL_LPC_GEN4_DEC, value.dword);
			break;
		default:	/* All other Intel chipset */
			pci_read_config_word(lpc_bridge, INTEL_LPC_GEN1_DEC, &(value.word));
			pci_reg_state.word = value.word;
			value.word = (INTEL_IOPORT_BASE & 0xfff1) | 0x1;
			pci_write_config_word(lpc_bridge, INTEL_LPC_GEN1_DEC, value.word);
		}
		break;
	case PCI_VENDOR_ID_ATI:
		pci_read_config_dword(lpc_bridge, ATI_LPC_REG, &(value.dword));
		pci_reg_state.dword = value.dword;
		value.dword = ((pci_reg_state.dword & 0x7f) | 0x80) << 0x10;
		pci_write_config_dword(lpc_bridge, ATI_LPC_REG, value.dword);
		break;
	default:
		BUG();
	}
	dprintk("Saved state of PCI register: [%x].\n", pci_reg_state.dword);

	if (check_default_state() || check_cdimode_flag(0)) {
		printk(O_ERR "EC state check failure, please report.\n");
		return -EIO;
	}

	return 0;

}

/*
 * Send a write command and associated data code to be written
 * Known commands an associated code significance:
 * 0xfbfd: Select Index with 'code' ordinal
 * 0xfbfe: Set to 'code' a previously selected Index
 * 0xfbfc: Set CDI mode flag
 */
static int send_ec_cmd(unsigned int command, u8 code)
{
	lowlevel_write(0xfbfc, 0x2);
	lowlevel_write(command, code);
	lowlevel_write(0xfbfc, 0x1);
	if (check_cdimode_flag(2))
		return -ETIME;
	return 0;
}

/*
 * Send a read command
 * Known commands an associated code significance:
 * 0xfbfe: Read a previously selected Index
 * 0xfbfc: Set CDI mode flag
 */
static int read_ec_cmd(unsigned int command, u8 * value)
{
	*value = lowlevel_read(command);
	lowlevel_write(0xfbfc, 0x1);
	if (check_cdimode_flag(2))
		return -ETIME;
	return 0;
}

/*
 * Disable EC Command/Data/Index PIO Access 
 * Step 1: clear_cdimode
 * Send Disable command
 * Revert PIO interface to conventional default state (0xf432 in the Command ports)
 * Step 2: clear_cdimode_pci
 * Disable the interface in the PCI config space of the Southbridge
 * These steps are separated due to constrains in error path treatement
 */
static void clear_cdimode(void)
{
	lowlevel_write(0xfbfc, 0x0);
	outb(0xf4, ioport_base + PIO_PORT_COMMAND1);
	outb(0x32, ioport_base + PIO_PORT_COMMAND2);
}

static void clear_cdimode_pci(void)
{
	switch (lpc_bridge->vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (lpc_bridge->device) {
		case PCI_DEVICE_ID_INTEL_ICH7_0:	/* ICH7 */
		case PCI_DEVICE_ID_INTEL_ICH7_1:
		case PCI_DEVICE_ID_INTEL_ICH7_30:
		case PCI_DEVICE_ID_INTEL_ICH7_31:
		case PCI_DEVICE_ID_INTEL_ICH8_4:	/* ICH8 */
			pci_write_config_dword(lpc_bridge, INTEL_LPC_GEN4_DEC, pci_reg_state.dword);
			break;
		default:	/* All other Intel chipset */
			pci_write_config_word(lpc_bridge, INTEL_LPC_GEN1_DEC, pci_reg_state.word);
		}
		break;
	case PCI_VENDOR_ID_ATI:
		pci_write_config_dword(lpc_bridge, ATI_LPC_REG, pci_reg_state.dword);
		break;
	default:
		BUG();
	}
}

/*
 * Try to init the backend
 * This function can be called blindly as it use a kref
 * to check if the init sequence was already done.
 */
static int omnibook_cdimode_init(const struct omnibook_operation *io_op)
{
	int retval = 0;
	int i;

	/* ectypes other than TSM70 have no business with this backend */
	if (!(omnibook_ectype & (TSM70 | TSX205)))
		return -ENODEV;

	if (io_op->backend->already_failed) {
		dprintk("CDI backend init already failed, skipping.\n");
		return -ENODEV;
	}

	if (!lpc_bridge) {
		/* Fist use of the backend */
		dprintk("Try to init cdimode\n");
		mutex_init(&io_op->backend->mutex);
		mutex_lock(&io_op->backend->mutex);
		kref_init(&io_op->backend->kref);

		/* PCI probing: find the LPC Super I/O bridge PCI device */
		for (i = 0; !lpc_bridge && lpc_bridge_table[i].vendor; ++i)
			lpc_bridge =
			    pci_get_device(lpc_bridge_table[i].vendor, lpc_bridge_table[i].device,
					   NULL);

		if (!lpc_bridge) {
			printk(O_ERR "Fail to find a supported LPC I/O bridge, please report\n");
			retval = -ENODEV;
			goto error1;
		}

		if ((retval = pci_enable_device(lpc_bridge))) {
			printk(O_ERR "Unable to enable PCI device.\n");
			goto error2;
		}

		switch (lpc_bridge->vendor) {
		case PCI_VENDOR_ID_INTEL:
			ioport_base = INTEL_IOPORT_BASE;
			break;
		case PCI_VENDOR_ID_ATI:
			ioport_base = ATI_IOPORT_BASE;
			break;
		default:
			BUG();
		}

		if (!request_region(ioport_base, 4, OMNIBOOK_MODULE_NAME)) {
			printk(O_ERR "Request I/O region error\n");
			retval = -ENODEV;
			goto error2;
		}

		/*
		 * Make an enable-check disable cycle for testing purpose
		 */

		retval = enable_cdimode();
		if (retval)
			goto error3;

		clear_cdimode();
		clear_cdimode_pci();

		dprintk("Cdimode init ok\n");
		mutex_unlock(&io_op->backend->mutex);
		return 0;
	} else {
		dprintk("Cdimode has already been initialized\n");
		kref_get(&io_op->backend->kref);
		return 0;
	}

      error3:
	clear_cdimode_pci();
	release_region(ioport_base, 4);
      error2:
	pci_dev_put(lpc_bridge);
	lpc_bridge = NULL;
      error1:
	io_op->backend->already_failed = 1;
	mutex_unlock(&io_op->backend->mutex);
	mutex_destroy(&io_op->backend->mutex);
	return retval;
}

static void cdimode_free(struct kref *ref)
{
	struct omnibook_backend *backend;
	
	dprintk("Cdimode not used anymore: disposing\n");

	backend = container_of(ref, struct omnibook_backend, kref);

	mutex_lock(&backend->mutex);
	pci_dev_put(lpc_bridge);
	release_region(ioport_base, 4);
	lpc_bridge = NULL;
	mutex_unlock(&backend->mutex);
	mutex_destroy(&backend->mutex);
}

static void omnibook_cdimode_exit(const struct omnibook_operation *io_op)
{
	/* ectypes other than TSM70 have no business with this backend */
	BUG_ON(!(omnibook_ectype & (TSM70 | TSX205)));
	dprintk("Trying to dispose cdimode\n");
	kref_put(&io_op->backend->kref, cdimode_free);
}

/* 
 * Read EC index and write result to value 
 * 'EC index' here is unrelated to an index in the EC registers
 */
static int omnibook_cdimode_read(const struct omnibook_operation *io_op, u8 * value)
{
	int retval = 0;

	if (!lpc_bridge)
		return -ENODEV;

	retval = enable_cdimode();
	if (retval)
		goto out;
	retval = send_ec_cmd(0xfbfd, (unsigned int)io_op->read_addr);
	if (retval)
		goto error;
	retval = read_ec_cmd(0xfbfe, value);

	if (io_op->read_mask)
		*value &= io_op->read_mask;

      error:
	clear_cdimode();
      out:
	clear_cdimode_pci();
	return retval;
}

/* 
 * Write value 
 * 'EC index' here is unrelated to an index in the EC registers
 */
static int omnibook_cdimode_write(const struct omnibook_operation *io_op, u8 value)
{
	int retval = 0;

	if (!lpc_bridge)
		return -ENODEV;

	retval = enable_cdimode();
	if (retval)
		goto out;
	retval = send_ec_cmd(0xfbfd, (unsigned int)io_op->write_addr);
	if (retval)
		goto error;
	retval = send_ec_cmd(0xfbfe, value);
      error:
	clear_cdimode();
      out:
	clear_cdimode_pci();
	return retval;

}

/*
 * Fn+foo and multimedia hotkeys handling
 */
static int omnibook_cdimode_hotkeys(const struct omnibook_operation *io_op, unsigned int state)
{
	int retval;

	struct omnibook_operation hotkeys_op = 
		{ CDI, 0, TSM70_FN_INDEX, 0, TSM70_FN_ENABLE, TSM70_FN_DISABLE};

	/* Fn+foo handling */
	retval = __omnibook_toggle(&hotkeys_op, !!(state & HKEY_FN));
	if (retval < 0)
		return retval;

	/* Multimedia keys handling */
	hotkeys_op.write_addr = TSM70_HOTKEYS_INDEX;
	hotkeys_op.on_mask = TSM70_HOTKEYS_ENABLE;
	hotkeys_op.off_mask = TSM70_HOTKEYS_DISABLE;
	retval = __omnibook_toggle(&hotkeys_op, !!(state & HKEY_MULTIMEDIA));

	return retval;
}

/* Scan index space, this hard locks my machine */
#if 0
static int compal_scan(char *buffer)
{
	int len = 0;
	int i, j;
	u8 v;

	for (i = 0; i < 255; i += 16) {
		for (j = 0; j < 16; j++) {
			omnibook_compal_read(i + j, &v);
			len += sprintf(buffer + len, "Read index %02x: %02x\n", i + j, v);
			mdelay(500);
		}
		if (j != 16)
			break;
	}

	return len;
}
#endif

struct omnibook_backend compal_backend = {
	.name = "compal",
	.hotkeys_write_cap = HKEY_MULTIMEDIA | HKEY_FN,
	.init = omnibook_cdimode_init,
	.exit = omnibook_cdimode_exit,
	.byte_read = omnibook_cdimode_read,
	.byte_write = omnibook_cdimode_write,
	.hotkeys_set = omnibook_cdimode_hotkeys,
};

/* End of file */
