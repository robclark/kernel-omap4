/*
 * polling.c -- scancode emulation for volume buttons
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
#include <linux/workqueue.h>
#include <linux/jiffies.h>

/*
 * XE3GC type key_polling polling:
 *
 * Polling interval for keys (100 ms)
 */

#define OMNIBOOK_POLL	msecs_to_jiffies(100)

/*
 * workqueue manipulations are mutex protected and thus kept in sync with key_polling_enabled
 */
static struct workqueue_struct *omnibook_wq;  
static int key_polling_enabled;
static DEFINE_MUTEX(poll_mutex);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
static void omnibook_key_poller(struct work_struct *work);
DECLARE_DELAYED_WORK(omnibook_poll_work, *omnibook_key_poller);
#else
static void omnibook_key_poller(void *data);
DECLARE_WORK(omnibook_poll_work, *omnibook_key_poller, NULL);
#endif

static struct omnibook_feature key_polling_driver;
static struct input_dev *poll_input_dev;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	static void omnibook_key_poller(struct work_struct *work)
#else
	static void omnibook_key_poller(void *data)
#endif
{
	u8 q0a;
	int retval;

	mutex_lock(&key_polling_driver.io_op->backend->mutex);
	__backend_byte_read(key_polling_driver.io_op, &q0a);
	__backend_byte_write(key_polling_driver.io_op, 0);
	mutex_unlock(&key_polling_driver.io_op->backend->mutex);

#ifdef CONFIG_OMNIBOOK_DEBUG
	if (unlikely(q0a & XE3GC_SLPB_MASK))
		dprintk("Sleep button pressed.\n");
	if (unlikely(q0a & XE3GC_F5_MASK))
		dprintk("Fn-F5 - LCD/CRT switch pressed.\n");
	if (unlikely(q0a & XE3GC_CNTR_MASK))
		dprintk("Fn+F3/Fn+F4 - Contrast up or down pressed.\n");
	if (unlikely(q0a & XE3GC_BRGT_MASK))
		dprintk("Fn+F1/Fn+F2 - Brightness up or down pressed.\n");
#endif

	/*
	 * Volume button scancode emulaton
	 * It emulates a key press and a release without repeat as other OneTouch buttons do.
	 */

	if (unlikely(q0a & XE3GC_VOLD_MASK)) {
		dprintk("Fn-down arrow or Volume down pressed.\n");
		omnibook_report_key(poll_input_dev, KEY_VOLUMEDOWN);
	}
	if (unlikely(q0a & XE3GC_VOLU_MASK)) {
		dprintk("Fn-up arrow or Volume up pressed.\n");
		omnibook_report_key(poll_input_dev, KEY_VOLUMEUP);
	}
	if (unlikely(q0a & XE3GC_MUTE_MASK)) {
		dprintk("Fn+F7 - Volume mute pressed.\n");
		omnibook_report_key(poll_input_dev, KEY_MUTE);
	}

	retval = queue_delayed_work(omnibook_wq, &omnibook_poll_work, OMNIBOOK_POLL);
	if(unlikely(!retval)) /* here non-zero on success */
		printk(O_ERR "Key_poller failed to rearm.\n");
}

static int omnibook_key_polling_enable(void)
{
	int retval = 0;

	if(mutex_lock_interruptible(&poll_mutex))
		return -ERESTARTSYS;

	if(key_polling_enabled)
		goto out;

	retval = !queue_delayed_work(omnibook_wq, &omnibook_poll_work, OMNIBOOK_POLL);
	if(retval)
		printk(O_ERR "Key_poller enabling failed.\n");
	else {	
		dprintk("Scancode emulation for volume buttons enabled.\n");
		key_polling_enabled = 1;
	}

	out:
	mutex_unlock(&poll_mutex);
	return retval;
}
	
static int omnibook_key_polling_disable(void)
{
	if(mutex_lock_interruptible(&poll_mutex))
		return -ERESTARTSYS;

	if(!key_polling_enabled)
		goto out;

	cancel_rearming_delayed_workqueue(omnibook_wq, &omnibook_poll_work);
	dprintk("Scancode emulation for volume buttons disabled.\n");
	key_polling_enabled = 0;

	out:
	mutex_unlock(&poll_mutex);
	return 0;
}


static int omnibook_key_polling_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	
	if(mutex_lock_interruptible(&poll_mutex))
		return -ERESTARTSYS;

	len += sprintf(buffer + len, "Volume buttons polling is %s.\n",
		(key_polling_enabled) ? "enabled" : "disabled");
#ifdef CONFIG_OMNIBOOK_DEBUG
	if(key_polling_enabled)	
		len += sprintf(buffer + len, "Will poll in %i msec.\n",
		jiffies_to_msecs(omnibook_poll_work.timer.expires - jiffies));
#endif
	mutex_unlock(&poll_mutex);
	return len;
}

static int omnibook_key_polling_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval;
	switch (*buffer) {
	case '0':
		retval = omnibook_key_polling_disable();
		break;
	case '1':
		retval = omnibook_key_polling_enable();
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}


/*
 * Stop polling upon suspend an restore it upon resume
 */
static int omnibook_key_polling_resume(struct omnibook_operation *io_op)
{
	int retval = 0;

	mutex_lock(&poll_mutex);
	if(key_polling_enabled)
		retval = !queue_delayed_work(omnibook_wq, &omnibook_poll_work, OMNIBOOK_POLL);
	mutex_unlock(&poll_mutex);
	return retval;	
}

static int omnibook_key_polling_suspend(struct omnibook_operation *io_op)
{
	mutex_lock(&poll_mutex);
	if(key_polling_enabled)
		cancel_rearming_delayed_workqueue(omnibook_wq, &omnibook_poll_work);
	mutex_unlock(&poll_mutex);
	return 0;
}

static int __init omnibook_key_polling_init(struct omnibook_operation *io_op)
{
	int retval = 0;	
	
	poll_input_dev = input_allocate_device();
	if (!poll_input_dev) {
		retval = -ENOMEM;
		goto out;
	}

	poll_input_dev->name = "Omnibook legacy laptop scancode generator";
	poll_input_dev->phys = "omnibook/input0";
	poll_input_dev->id.bustype = BUS_HOST;
	
	/* this device has three keys */
	set_bit(EV_KEY, poll_input_dev->evbit);
	set_bit(KEY_VOLUMEDOWN, poll_input_dev->keybit);
	set_bit(KEY_VOLUMEUP, poll_input_dev->keybit);
	set_bit(KEY_MUTE, poll_input_dev->keybit);

	retval = input_register_device(poll_input_dev);
	if (retval) {
		input_free_device(poll_input_dev);
		goto out;
	}

	omnibook_wq = create_singlethread_workqueue("omnibook");
	if(!omnibook_wq)
		retval = -ENOMEM;
	else
		retval = omnibook_key_polling_enable();

out:
	return retval;
}

static void __exit omnibook_key_polling_cleanup(struct omnibook_operation *io_op)
{
	omnibook_key_polling_disable();	
	destroy_workqueue(omnibook_wq);
	input_unregister_device(poll_input_dev);
}

static struct omnibook_tbl key_polling_table[] __initdata = {
	{XE3GC, SIMPLE_BYTE(EC, XE3GC_Q0A, 0)},
	{0,}
};

static struct omnibook_feature __declared_feature key_polling_driver = {
	.name = "key_polling",
	.enabled = 0, /* dangerous */
	.read = omnibook_key_polling_read,
	.write = omnibook_key_polling_write,
	.init = omnibook_key_polling_init,
	.exit = omnibook_key_polling_cleanup,
	.suspend = omnibook_key_polling_suspend,
	.resume = omnibook_key_polling_resume,
	.ectypes = XE3GC,
	.tbl = key_polling_table,
};

module_param_named(key_polling, key_polling_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(key_polling, "Use 0 to disable, 1 to enable key polling");
/* End of file */
