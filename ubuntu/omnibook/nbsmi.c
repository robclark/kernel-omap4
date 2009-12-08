/*
 * nbsmi.c -- Toshiba SMI low-level acces code
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
 * Sources of inspirations for this code were:
 * -Toshiba via provided hardware specification
 * -Thorsten Zachmann with the 's1bl' project
 * -Frederico Munoz with the 'tecra_acpi' project
 * Thanks to them
 */

#include "omnibook.h"
#include "hardware.h"
#include <linux/preempt.h>
#include <linux/pci.h>
#include <linux/kref.h>
#include <asm/io.h>
#include <asm/mc146818rtc.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

/* copied from drivers/input/serio/i8042-io.h */
#define I8042_KBD_PHYS_DESC "isa0060/serio0"

/*
 * ATI's IXP PCI-LPC bridge
 */
#define INTEL_PMBASE	0x40
#define INTEL_GPE0_EN	0x2c

#define BUFFER_SIZE	0x20
#define INTEL_OFFSET	0x60
#define	INTEL_SMI_PORT	0xb2	/* APM_CNT port in INTEL ICH specs */

/*
 * Toshiba Specs state 0xef here but:
 * -this would overflow (ef + 19 > ff)
 * -code from Toshiba use e0, which make much more sense
 */

#define ATI_OFFSET	0xe0
#define	ATI_SMI_PORT	0xb0

#define	EC_INDEX_PORT	0x300
#define EC_DATA_PORT	0x301

/* Masks decode for GetAeral */
#define WLEX_MASK 	0x4
#define WLAT_MASK	0x8
#define BTEX_MASK	0x1
#define BTAT_MASK	0x2

/*
 * Private data of this backend
 */
struct nbsmi_backend_data {
	struct pci_dev *lpc_bridge;	/* Southbridge chip ISA bridge/LPC interface PCI device */
	u8 start_offset;		/* Start offset in CMOS memory */
	struct input_dev *nbsmi_input_dev;
	struct work_struct fnkey_work;
};

/*
 * Possible list of supported southbridges
 * Here mostly to implement a more or less clean PCI probing
 * Works only because of previous DMI probing.
 * It's in compal.c
 */
extern const struct pci_device_id lpc_bridge_table[];

/*
 * Since we are going to trigger an SMI, all registers (I assume this does not
 * include esp and maybe ebp) and eflags may be mangled in the
 * process. 
 * We also disable preemtion and IRQs upon SMI call.
 */
static inline u32 ati_do_smi_call(u16 function)
{
	unsigned long flags;
	u32 retval = 0;

	local_irq_save(flags);
	preempt_disable();
	
/*
 * eflags, eax, ebx, ecx, edx, esi and edi are clobbered upon writing to SMI_PORT
 * thus the clobber list.
 *
 * Equivalent pseudocode:
 *
 * eax = function; [non null]
 * outw(eax, ATI_SMI_PORT); <- This Trigger an SMI
 * if( eax == 0 ) [success if eax has been cleared]
 * 	goto out;
 * if( inb(ATI_SMI_PORT + 1) == 0) [if not in eax, success maybe be stored here]
 *	goto out;
 * retval = -EIO; [too bad]
 * out:
 */
	__asm__ __volatile__("outw  %%ax, %2;	\
			      orw %%ax, %%ax;	\
			      jz 1f;		\
			      inw %3, %%ax;	\
			      orw %%ax, %%ax;	\
			      jz 1f;		\
			      movl %4, %0;	\
			      1:;"
			     : "=m" (retval)
			     : "a"(function), "N"(ATI_SMI_PORT), "N"(ATI_SMI_PORT+1), "i"(-EIO)
			     : "memory", "ebx", "ecx", "edx", "esi", "edi", "cc");

	local_irq_restore(flags);
	preempt_enable_no_resched();
	return retval;
}

static inline u32 intel_do_smi_call(u16 function, struct pci_dev *lpc_bridge)
{
	u32 state;
	unsigned long flags;
	u32 retval = 0;
	u32 sci_en = 0;

	local_irq_save(flags);
	preempt_disable();

/* 
 * We get the PMBASE offset ( bits 15:7 at 0x40 offset of PCI config space )
 * And we access offset 2c (GPE0_EN), save the state, disable all SCI
 * and restore the state after the SMI call
 */			
	pci_read_config_dword(lpc_bridge, INTEL_PMBASE, &sci_en);
	sci_en = sci_en & 0xff80; /* Keep bits 15:7 */
	sci_en += INTEL_GPE0_EN;  /* GPEO_EN offset */
	state = inl(sci_en);
	outl(0, sci_en);

/*
 * eflags, eax, ebx, ecx, edx, esi and edi are clobbered upon writing to SMI_PORT
 * thus the clobber list.
 *
 * Equivalent pseudocode:
 *
 * eax = function; [non null]
 * outw(eax, INTEL_SMI_PORT); <- This Trigger an SMI
 * if( eax == 0 ) [success if eax has been cleared]
 * 	goto out; 
 * retval = -EIO; [too bad]
 * out:
 */
	__asm__ __volatile__("outw %%ax, %2;	\
			      orw %%ax, %%ax;	\
			      jz 1f;		\
			      movl %3, %0;	\
			      1:;"
			     : "=m" (retval)
			     : "a"(function), "N"(INTEL_SMI_PORT), "i"(-EIO)
			     : "memory", "ebx", "ecx", "edx", "esi", "edi", "cc");

	outl(state, sci_en);
	local_irq_restore(flags);
	preempt_enable_no_resched();
	return retval;
}

static int nbsmi_smi_command(u16 function, 
			     const u8 * inputbuffer,
			     u8 * outputbuffer,
			     const struct nbsmi_backend_data *priv_data)
{
	int count;
	u32 retval = 0;
	

	for (count = 0; count < BUFFER_SIZE; count++) {
		outb(count + priv_data->start_offset, RTC_PORT(2));
		outb(*(inputbuffer + count), RTC_PORT(3));
	}

/* 
 * We have to write 0xe4XX to smi_port
 * where XX is the SMI function code
 */
	function = (function & 0xff) << 8;
	function |= 0xe4;

	switch (priv_data->lpc_bridge->vendor) {
	case PCI_VENDOR_ID_INTEL:
		retval = intel_do_smi_call(function, priv_data->lpc_bridge);
		break;
	case PCI_VENDOR_ID_ATI:
		retval = ati_do_smi_call(function);
		break;
	default:
		BUG();
	}

	if (retval)
		printk(O_ERR "smi_command failed with error %u.\n", retval);

	for (count = 0; count < BUFFER_SIZE; count++) {
		outb(count + priv_data->start_offset, RTC_PORT(2));
		*(outputbuffer + count) = inb(RTC_PORT(3));
	}

	return retval;
}

static int nbsmi_smi_read_command(const struct omnibook_operation *io_op, u8 * data)
{
	int retval;
	u8 *inputbuffer;
	u8 *outputbuffer;
	struct nbsmi_backend_data *priv_data = io_op->backend->data;

	if (!priv_data)
		return -ENODEV;

	inputbuffer = kcalloc(BUFFER_SIZE, sizeof(u8), GFP_KERNEL);
	if (!inputbuffer) {
		retval = -ENOMEM;
		goto error1;
	}

	outputbuffer = kcalloc(BUFFER_SIZE, sizeof(u8), GFP_KERNEL);
	if (!outputbuffer) {
		retval = -ENOMEM;
		goto error2;
	}

	retval = nbsmi_smi_command((u16) io_op->read_addr, inputbuffer, outputbuffer, priv_data);
	if (retval)
		goto out;

	*data = outputbuffer[0];

	if (io_op->read_mask)
		*data &= io_op->read_mask;

      out:
	kfree(outputbuffer);
      error2:
	kfree(inputbuffer);
      error1:
	return retval;
}

static int nbsmi_smi_write_command(const struct omnibook_operation *io_op, u8 data)
{
	int retval;
	u8 *inputbuffer;
	u8 *outputbuffer;
	struct nbsmi_backend_data *priv_data = io_op->backend->data;

	if (!priv_data)
		return -ENODEV;

	inputbuffer = kcalloc(BUFFER_SIZE, sizeof(u8), GFP_KERNEL);
	if (!inputbuffer) {
		retval = -ENOMEM;
		goto error1;
	}

	outputbuffer = kcalloc(BUFFER_SIZE, sizeof(u8), GFP_KERNEL);
	if (!outputbuffer) {
		retval = -ENOMEM;
		goto error2;
	}

	inputbuffer[0] = data;

	retval = nbsmi_smi_command((u16) io_op->write_addr, inputbuffer, outputbuffer, priv_data);

	kfree(outputbuffer);
      error2:
	kfree(inputbuffer);
      error1:
	return retval;
}

/*
 * Read/Write to INDEX/DATA interface at port 0x300 (SMSC Mailbox registers)
 */
static inline void nbsmi_ec_read_command(u8 index, u8 * data)
{
	outb(index, EC_INDEX_PORT);
	*data = inb(EC_DATA_PORT);
}

#if 0
static inline void nbsmi_ec_write_command(u8 index, u8 data)
{
	outb(index, EC_INDEX_PORT);
	outb(data, EC_DATA_PORT);
}
#endif


/*
 * Hotkeys workflow:
 * 1. Fn+Foo pressed
 * 2. Scancode 0x6d generated by kbd controller
 * 3. Scancode 0x6d caught by omnibook input handler
 * 4. SMI Call issued -> Got keycode of last actually pressed Fn key
 * 5. nbsmi_scan_table used to associate a detected keycode with a generated one
 * 6. Generated keycode issued using the omnibook input device
 */

/*
 * The input handler should only bind with the standard AT keyboard.
 * XXX: Scancode 0x6d won't be detected if the keyboard has already been
 * grabbed (the Xorg event input driver do that)
 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,21))
static int hook_connect(struct input_handler *handler,
					 struct input_dev *dev,
					 const struct input_device_id *id)
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
static struct input_handle *hook_connect(struct input_handler *handler,
					 struct input_dev *dev,
					 const struct input_device_id *id)
#else
static struct input_handle *hook_connect(struct input_handler *handler,
					 struct input_dev *dev,
					 struct input_device_id *id)
#endif
{
	struct input_handle *handle;
	int error;

	/* the 0x0001 vendor magic number is found in atkbd.c */
	if(!(dev->id.bustype == BUS_I8042 && dev->id.vendor == 0x0001))
		goto out_nobind;

	if(!strstr(dev->phys, I8042_KBD_PHYS_DESC))
		goto out_nobind;

	dprintk("hook_connect for device %s.\n", dev->name);

	if(dev->grab)
		printk(O_WARN "Input device is grabbed by %s, Fn hotkeys won't work.\n",
			dev->grab->name);

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,21))
		return -ENOMEM;
#else
		return NULL;
#endif

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "omnibook_scancode_hook";
	handle->private = handler->private;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,21))
	error = input_register_handle(handle);
	if (error) {
		dprintk("register_handle failed\n");
		goto out_nobind_free;
	} 
	error = input_open_device(handle);
	if (error) {
		dprintk("register_handle failed\n");
		input_unregister_handle(handle);
		goto out_nobind_free;
	} 
	
#else
	error = input_open_device(handle);
	if (error==0) dprintk("Input device opened\n");
	else { 
		dprintk("opening input device failed\n");
		goto out_nobind_free;
	}
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,21))
	return 0;
out_nobind_free:
	kfree(handle);
out_nobind:
	return -ENODEV;
#else
	return handle;
out_nobind_free:
	kfree(handle);
out_nobind:
	return NULL;
#endif	
}

static void hook_disconnect(struct input_handle *handle)
{
	dprintk("hook_disconnect.\n");
	input_close_device(handle);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,21))
	input_unregister_handle(handle);
#endif
	kfree(handle);
}

/*
 * Hook for scancode 0x6d. Actual handling is done in a workqueue as 
 * the nbsmi backend might sleep.
 */

static void hook_event(struct input_handle *handle, unsigned int event_type,
		      unsigned int event_code, int value)
{
	if (event_type == EV_MSC && event_code == MSC_SCAN && value == SMI_FN_SCAN)
		schedule_work(&((struct nbsmi_backend_data *)handle->private)->fnkey_work);
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
static const struct input_device_id hook_ids[] = {
#else
static struct input_device_id hook_ids[] = {
#endif
	{
                .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
                .evbit = { BIT(EV_KEY) },
        },
	{ },    /* Terminating entry */
};

static struct input_handler hook_handler = {
	.event		= hook_event,
	.connect	= hook_connect,
	.disconnect	= hook_disconnect,
	.name		= OMNIBOOK_MODULE_NAME,
	.id_table	= hook_ids,
};

/*
 * Define some KEY_ that may be missing in input.h for some kernel versions
 */
#ifndef KEY_WLAN
#define KEY_WLAN 238
#endif 

/*
 * Detected scancode to keycode table
 */
static const struct {
	unsigned int scancode;
	unsigned int keycode;
} nbsmi_scan_table[] = {
	{ KEY_ESC, KEY_MUTE},
	{ KEY_F1, KEY_FN_F1},
	{ KEY_F2, KEY_PROG1},
	{ KEY_F3, KEY_SLEEP},
	{ KEY_F4, KEY_SUSPEND},
	{ KEY_F5, KEY_SWITCHVIDEOMODE},
	{ KEY_F6, KEY_BRIGHTNESSDOWN},
	{ KEY_F7, KEY_BRIGHTNESSUP},
	{ KEY_F8, KEY_WLAN},
	{ KEY_F9, KEY_FN_F9},
	{ KEY_SPACE, KEY_ZOOM},
	{ 0,0},
};

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
static void omnibook_handle_fnkey(struct work_struct *work);
#else
static void omnibook_handle_fnkey(void* data);
#endif

/*
 * Register the input handler and the input device in the input subsystem
 */
static int register_input_subsystem(struct nbsmi_backend_data *priv_data)
{
	int i, retval = 0;
	struct input_dev *nbsmi_input_dev;

	nbsmi_input_dev = input_allocate_device();
	if (!nbsmi_input_dev) {
		retval = -ENOMEM;
		goto out;
	}

	nbsmi_input_dev->name = "Omnibook NbSMI scancode generator";
	nbsmi_input_dev->phys = "omnibook/input0";
	nbsmi_input_dev->id.bustype = BUS_HOST;
	
	set_bit(EV_KEY, nbsmi_input_dev->evbit);
	
	for(i=0 ; i < ARRAY_SIZE(nbsmi_scan_table); i++)
		set_bit(nbsmi_scan_table[i].keycode, nbsmi_input_dev->keybit);

	retval = input_register_device(nbsmi_input_dev);
	if(retval) {
		input_free_device(nbsmi_input_dev);
		goto out;
	}

	priv_data->nbsmi_input_dev = nbsmi_input_dev;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_WORK(&priv_data->fnkey_work, *omnibook_handle_fnkey);
#else
	INIT_WORK(&priv_data->fnkey_work, *omnibook_handle_fnkey, priv_data);
#endif


	hook_handler.private = priv_data;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	retval = input_register_handler(&hook_handler);	
#else
	input_register_handler(&hook_handler);
#endif

	out:	
	return retval;
}

/*
 * Try to init the backend
 * This function can be called blindly as it use a kref
 * to check if the init sequence was already done.
 */
static int omnibook_nbsmi_init(const struct omnibook_operation *io_op)
{
	int retval = 0;
	int i;
	u8 ec_data;
	u32 smi_port = 0;
	struct nbsmi_backend_data *priv_data;

	/* ectypes other than TSM40 have no business with this backend */
	if (!(omnibook_ectype & TSM40))
		return -ENODEV;

	if (io_op->backend->already_failed) {
		dprintk("NbSmi backend init already failed, skipping.\n");
		return -ENODEV;
	}

	if (!io_op->backend->data) {
		/* Fist use of the backend */
		dprintk("Try to init NbSmi\n");
		mutex_init(&io_op->backend->mutex);
		mutex_lock(&io_op->backend->mutex);
		kref_init(&io_op->backend->kref);

		priv_data = kzalloc(sizeof(struct nbsmi_backend_data), GFP_KERNEL);
		if (!priv_data) {
			retval = -ENOMEM;
			goto error0;
		}

		/* PCI probing: find the LPC Super I/O bridge PCI device */
		for (i = 0; !priv_data->lpc_bridge && lpc_bridge_table[i].vendor; ++i)
			priv_data->lpc_bridge =
			    pci_get_device(lpc_bridge_table[i].vendor, lpc_bridge_table[i].device,
					   NULL);

		if (!priv_data->lpc_bridge) {
			printk(O_ERR "Fail to find a supported LPC I/O bridge, please report\n");
			retval = -ENODEV;
			goto error1;
		}

		if ((retval = pci_enable_device(priv_data->lpc_bridge))) {
			printk(O_ERR "Unable to enable PCI device.\n");
			goto error2;
		}

		switch (priv_data->lpc_bridge->vendor) {
		case PCI_VENDOR_ID_INTEL:
			priv_data->start_offset = INTEL_OFFSET;
			smi_port = INTEL_SMI_PORT;
			break;
		case PCI_VENDOR_ID_ATI:
			priv_data->start_offset = ATI_OFFSET;
			smi_port = ATI_SMI_PORT;
			break;
		default:
			BUG();
		}

		if (!request_region(smi_port, 2, OMNIBOOK_MODULE_NAME)) {
			printk(O_ERR "Request SMI I/O region error\n");
			retval = -ENODEV;
			goto error2;
		}

		if (!request_region(EC_INDEX_PORT, 2, OMNIBOOK_MODULE_NAME)) {
			printk(O_ERR "Request EC I/O region error\n");
			retval = -ENODEV;
			goto error3;
		}

		/*
		 * Try some heuristic tests to avoid enabling this interface on unsuported laptops:
		 * See what a port 300h read index 8f gives. Guess there is nothing if read 0xff
		 */

		nbsmi_ec_read_command(SMI_FN_PRESSED, &ec_data);
		dprintk("NbSmi test probe read: %x\n", ec_data);
		if (ec_data == 0xff) {
			printk(O_ERR "Probing at SMSC Mailbox registers failed, disabling NbSmi\n");
			retval = -ENODEV;
			goto error4;
		}

		retval = register_input_subsystem(priv_data);
		if(retval)
			goto error4;

		io_op->backend->data = priv_data;

		dprintk("NbSmi init ok\n");
		mutex_unlock(&io_op->backend->mutex);
		return 0;
	} else {
		dprintk("NbSmi has already been initialized\n");
		kref_get(&io_op->backend->kref);
		return 0;
	}
      error4:
	release_region(EC_INDEX_PORT, 2);
      error3:
	release_region(smi_port, 2);
      error2:
	pci_dev_put(priv_data->lpc_bridge);
      error1:
	kfree(priv_data);
	io_op->backend->data = NULL;
      error0:
	io_op->backend->already_failed = 1;
	mutex_unlock(&io_op->backend->mutex);
	mutex_destroy(&io_op->backend->mutex);
	return retval;
}

/*
 * Free all allocated stuff and unregister from the input subsystem
 */
static void nbsmi_free(struct kref *ref)
{
	u32 smi_port = 0;
	struct omnibook_backend *backend;
	struct nbsmi_backend_data *priv_data;

	dprintk("NbSmi not used anymore: disposing\n");

	backend = container_of(ref, struct omnibook_backend, kref);
	priv_data = backend->data;

	flush_scheduled_work();
	input_unregister_handler(&hook_handler);
	input_unregister_device(priv_data->nbsmi_input_dev);

	mutex_lock(&backend->mutex);

	switch (priv_data->lpc_bridge->vendor) {
	case PCI_VENDOR_ID_INTEL:
		smi_port = INTEL_SMI_PORT;
		break;
	case PCI_VENDOR_ID_ATI:
		smi_port = ATI_SMI_PORT;
		break;
	default:
		BUG();
	}

	pci_dev_put(priv_data->lpc_bridge);
	release_region(smi_port, 2);
	release_region(EC_INDEX_PORT, 2);
	kfree(priv_data);
	backend->data = NULL;
	mutex_unlock(&backend->mutex);
	mutex_destroy(&backend->mutex);
}

static void omnibook_nbsmi_exit(const struct omnibook_operation *io_op)
{
	/* ectypes other than TSM40 have no business with this backend */
	BUG_ON(!(omnibook_ectype & TSM40));
	dprintk("Trying to dispose NbSmi\n");
	kref_put(&io_op->backend->kref, nbsmi_free);
}

/*
 * Adjust the lcd backlight level by delta.
 * Used for Fn+F6/F7 keypress
 */
static int adjust_brighness(int delta)
{
	struct omnibook_feature *lcd_feature = omnibook_find_feature("lcd");
	struct omnibook_operation *io_op;
	int retval = 0;
	u8 brgt;

	if(!lcd_feature)
		return -ENODEV;

	io_op = lcd_feature->io_op;

	mutex_lock(&io_op->backend->mutex);

	if(( retval = __backend_byte_read(io_op, &brgt)))
		goto out;	

	dprintk("FnF6/F7 pressed: adjusting britghtnes.\n");

	if (((int) brgt + delta) < 0)
		brgt = 0;
	else if ((brgt + delta) > omnibook_max_brightness)
		brgt = omnibook_max_brightness;
	else
		brgt += delta;

	retval = __backend_byte_write(io_op, brgt);

	out:
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

static const struct omnibook_operation last_scan_op = SIMPLE_BYTE(SMI,SMI_GET_FN_LAST_SCAN,0);

/*
 * Workqueue handler for Fn hotkeys
 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
static void omnibook_handle_fnkey(struct work_struct *work)
#else
static void omnibook_handle_fnkey(void* data)
#endif
{
	int i;
	u8 gen_scan;
	struct input_dev *input_dev;

	if(backend_byte_read(&last_scan_op, &gen_scan))
		return;

	dprintk("detected scancode %x.\n", gen_scan);
	switch(gen_scan) {
	case KEY_F6:
		adjust_brighness(-1);
		break;
	case KEY_F7:
		adjust_brighness(+1);
		break;
	}

	for(i = 0 ; i < ARRAY_SIZE(nbsmi_scan_table); i++) {
		if( gen_scan == nbsmi_scan_table[i].scancode) {
			dprintk("generating keycode %i.\n", nbsmi_scan_table[i].keycode);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
			input_dev = container_of(work, struct nbsmi_backend_data, fnkey_work)->nbsmi_input_dev;
#else
			input_dev = ((struct nbsmi_backend_data *) data)->nbsmi_input_dev;
#endif
			omnibook_report_key(input_dev, nbsmi_scan_table[i].keycode);
			break;
		}
	}
}

static int omnibook_nbsmi_get_wireless(const struct omnibook_operation *io_op, unsigned int *state)
{
	int retval = 0;
	struct omnibook_operation aerial_op = SIMPLE_BYTE(SMI, SMI_GET_KILL_SWITCH, 0);
	u8 data;

	if ((retval = nbsmi_smi_read_command(&aerial_op, &data)))
		goto out;

	dprintk("get_wireless (kill switch) raw_state: %x\n", data);

	*state = data ? KILLSWITCH : 0;

	aerial_op.read_addr = SMI_GET_AERIAL;

	if ((retval = nbsmi_smi_read_command(&aerial_op, &data)))
		goto out;

	dprintk("get_wireless (aerial) raw_state: %x\n", data);

	*state |= (data & WLEX_MASK) ? WIFI_EX : 0;
	*state |= (data & WLAT_MASK) ? WIFI_STA : 0;
	*state |= (data & BTEX_MASK) ? BT_EX : 0;
	*state |= (data & BTAT_MASK) ? BT_STA : 0;

      out:
	return retval;
}

static int omnibook_nbsmi_set_wireless(const struct omnibook_operation *io_op, unsigned int state)
{
	int retval = 0;
	u8 data;
	struct omnibook_operation aerial_op = SIMPLE_BYTE(SMI, SMI_SET_AERIAL, 0);

	data = !!(state & BT_STA);
	data |= !!(state & WIFI_STA) << 0x1;

	dprintk("set_wireless raw_state: %x\n", data);

	retval = nbsmi_smi_write_command(&aerial_op, data);

	return retval;
}

static int omnibook_nbmsi_hotkeys_get(const struct omnibook_operation *io_op, unsigned int *state)
{
	int retval;
	u8 data = 0;
	struct omnibook_operation hotkeys_op = SIMPLE_BYTE(SMI, SMI_GET_FN_INTERFACE, 0);

	retval = nbsmi_smi_read_command(&hotkeys_op, &data);
	if (retval < 0)
		return retval;

	dprintk("get_hotkeys raw_state: %x\n", data);

	*state = (data & SMI_FN_KEYS_MASK) ? HKEY_FN : 0;
	*state |= (data & SMI_STICK_KEYS_MASK) ? HKEY_STICK : 0;
	*state |= (data & SMI_FN_TWICE_LOCK_MASK) ? HKEY_TWICE_LOCK : 0;
	*state |= (data & SMI_FN_DOCK_MASK) ? HKEY_DOCK : 0;

	return 0;
}


static int omnibook_nbmsi_hotkeys_set(const struct omnibook_operation *io_op, unsigned int state)
{
	int i, retval;
	u8 data, rdata;
	struct omnibook_operation hotkeys_op = SIMPLE_BYTE(SMI, SMI_SET_FN_F5_INTERFACE, 0);	
	u8* data_array;

	data = !!(state & HKEY_FNF5);

	dprintk("set_hotkeys (Fn F5) raw_state: %x\n", data);

	retval = nbsmi_smi_write_command(&hotkeys_op, data);
	if (retval < 0)
		return retval;

	hotkeys_op.write_addr = SMI_SET_FN_INTERFACE;
	hotkeys_op.read_addr =	SMI_GET_FN_INTERFACE;

	data = (state & HKEY_FN) ? SMI_FN_KEYS_MASK : 0;
	data |= (state & HKEY_STICK) ? SMI_STICK_KEYS_MASK : 0;
	data |= (state & HKEY_TWICE_LOCK) ? SMI_FN_TWICE_LOCK_MASK : 0;
	data |= (state & HKEY_DOCK) ? SMI_FN_DOCK_MASK : 0;

	dprintk("set_hotkeys (Fn interface) raw_state: %x\n", data);

	/*
	 * Hardware seems to be quite stubborn and multiple retries may be
	 * required. The criteria here is simple: retry until probed state match
	 * the requested one (with timeout).
	 */

	data_array = kcalloc(250, sizeof(u8), GFP_KERNEL);
	if(!data_array)
		return -ENODEV;

	for (i = 0; i < 250; i++) {
		retval = nbsmi_smi_write_command(&hotkeys_op, data);
		if (retval)
			goto out;
		mdelay(1);
		retval = nbsmi_smi_read_command(&hotkeys_op, &rdata);
		if(retval)
			goto out;
		data_array[i] = rdata;
		if(rdata == data) {
			dprintk("check loop ok after %i iters\n.",i);
			retval = 0;
			goto out;
		}
	}
	dprintk("error or check loop timeout !!\n");
	dprintk("forensics datas: ");
	for (i = 0; i < 250; i++)
		dprintk_simple("%x ", data_array[i]);
	dprintk_simple("\n");
out:
	kfree(data_array);
	return retval;
}

static const unsigned int nbsmi_display_mode_list[] = {
	DISPLAY_LCD_ON,
	DISPLAY_LCD_ON | DISPLAY_CRT_ON,
	DISPLAY_CRT_ON,
	DISPLAY_LCD_ON | DISPLAY_TVO_ON,
	DISPLAY_TVO_ON,
};

static int omnibook_nbmsi_display_get(const struct omnibook_operation *io_op, unsigned int *state)
{
	int retval = 0;
	u8 data;

	retval = nbsmi_smi_read_command(io_op, &data);
	if (retval < 0)
		return retval;

	if (data > (ARRAY_SIZE(nbsmi_display_mode_list) - 1))
		return -EIO;

	*state = nbsmi_display_mode_list[data];

	return DISPLAY_LCD_ON | DISPLAY_CRT_ON | DISPLAY_TVO_ON;
}

static int omnibook_nbmsi_display_set(const struct omnibook_operation *io_op, unsigned int state)
{
	int retval;
	int i;
	u8 matched = 255;

	for (i = 0; i < ARRAY_SIZE(nbsmi_display_mode_list); i++) {
		if (nbsmi_display_mode_list[i] == state) {
			matched = i;
			break;
		}
	}

	if(matched == 255) {
		printk(O_ERR "Display mode %x is unsupported.\n", state);
		return -EINVAL;
	}

	retval = nbsmi_smi_write_command(io_op, matched);
	if (retval < 0)
		return retval;

	return DISPLAY_LCD_ON | DISPLAY_CRT_ON | DISPLAY_TVO_ON;
}

struct omnibook_backend nbsmi_backend = {
	.name = "nbsmi",
	.hotkeys_read_cap = HKEY_FN | HKEY_STICK | HKEY_TWICE_LOCK | HKEY_DOCK,
	.hotkeys_write_cap = HKEY_FN | HKEY_STICK | HKEY_TWICE_LOCK | HKEY_DOCK | HKEY_FNF5,
	.init = omnibook_nbsmi_init,
	.exit = omnibook_nbsmi_exit,
	.byte_read = nbsmi_smi_read_command,
	.byte_write = nbsmi_smi_write_command,
	.aerial_get = omnibook_nbsmi_get_wireless,
	.aerial_set = omnibook_nbsmi_set_wireless,
	.hotkeys_get = omnibook_nbmsi_hotkeys_get,
	.hotkeys_set = omnibook_nbmsi_hotkeys_set,
	.display_get = omnibook_nbmsi_display_get,
	.display_set = omnibook_nbmsi_display_set,
};
