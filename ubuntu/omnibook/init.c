/*
 * init.c -- module initialization code
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

#include <linux/proc_fs.h>
#include <linux/dmi.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include "hardware.h"
#include "laptop.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
#include <linux/platform_device.h>
#else
#include <linux/device.h>
#endif

/*
 * For compatibility with kernel older than 2.6.11
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11))
typedef u32 pm_message_t;
#endif

static int __init omnibook_probe(struct platform_device *dev);
static int __exit omnibook_remove(struct platform_device *dev);
static int omnibook_suspend(struct platform_device *dev, pm_message_t state);
static int omnibook_resume(struct platform_device *dev);

/*
 * For compatibility with kernel older than 2.6.15
 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15))

#define to_platform_device(x) container_of((x), struct platform_device, dev)

static int __init compat_omnibook_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	return omnibook_probe(pdev);
}

static int __exit compat_omnibook_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	return omnibook_remove(pdev);
}

static int compat_omnibook_suspend(struct device *dev, pm_message_t state, u32 level)
{
	struct platform_device *pdev = to_platform_device(dev);
	return omnibook_suspend(pdev, state);
}

static int compat_omnibook_resume(struct device *dev, u32 level)
{
	struct platform_device *pdev = to_platform_device(dev);
	return omnibook_resume(pdev);
}

#endif

static struct proc_dir_entry *omnibook_proc_root = NULL;

enum omnibook_ectype_t omnibook_ectype = NONE;

static const char *laptop_model __initdata;

static int omnibook_userset = 0;

/*
 * The platform_driver interface was added in linux 2.6.15
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))

static struct platform_device *omnibook_device;

static struct platform_driver omnibook_driver = {
	.probe = omnibook_probe,
	.remove = omnibook_remove,
#ifdef CONFIG_PM
	.suspend = omnibook_suspend,
	.resume = omnibook_resume,
#endif
	.driver = {
		   .name = OMNIBOOK_MODULE_NAME,
		   .owner = THIS_MODULE,
		   },
};

#else				/* 2.6.15 */

static struct device_driver omnibook_driver = {
	.name = OMNIBOOK_MODULE_NAME,
	.bus = &platform_bus_type,
	.probe = compat_omnibook_probe,
	.remove = compat_omnibook_remove,
#ifdef CONFIG_PM
	.suspend = compat_omnibook_suspend,
	.resume = compat_omnibook_resume,
#endif
};

static struct platform_device omnibook_device = {
	.name = OMNIBOOK_MODULE_NAME,
};

#endif				/* 2.6.15 */

/* Linked list of all enabled features */
static struct omnibook_feature *omnibook_available_feature;

/* Delimiters of the .features section wich holds all the omnibook_feature structs */
extern struct omnibook_feature _start_features_driver[];
extern struct omnibook_feature _end_features_driver[];

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
static int __init dmi_matched(struct dmi_system_id *dmi)
#else
static int __init dmi_matched(const struct dmi_system_id *dmi)
#endif
{
	omnibook_ectype = (enum omnibook_ectype_t)dmi->driver_data;
	if (dmi->ident)
		laptop_model = (char *)dmi->ident;
	else
		laptop_model = dmi_get_system_info(DMI_PRODUCT_VERSION);
	return 1;		/* return non zero means we stop the parsing selecting this entry */
}

/* 
 * Callback function for procfs file reading: the name of the file read was stored in *data 
 */
static int procfile_read_dispatch(char *page, char **start, off_t off, int count, int *eof,
				  void *data)
{
	struct omnibook_feature *feature = (struct omnibook_feature *)data;
	int len = 0;

	if (!feature || !feature->read)
		return -EINVAL;

	if(off)
		goto out;

	len = feature->read(page, feature->io_op);
	if (len < 0)
		return len;

	out:
	*eof = 1;
	return len;
}

/* 
 * Callback function for procfs file writing: the name of the file written was stored in *data 
 */
static int procfile_write_dispatch(struct file *file, const char __user * userbuf,
				   unsigned long count, void *data)
{
	struct omnibook_feature *feature = (struct omnibook_feature *)data;
	char *kernbuf;
	int retval;

	if (!feature || !feature->write)
		return -EINVAL;

	kernbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kernbuf)
		return -ENOMEM;

	if (copy_from_user(kernbuf, userbuf, count)) {
		kfree(kernbuf);
		return -EFAULT;
	}

	/* Make sure the string is \0 terminated */
	kernbuf[count] = '\0';

	retval = feature->write(kernbuf, feature->io_op);
	if (retval == 0)
		retval = count;

	kfree(kernbuf);

	return retval;
}

/*
 * Match an ectype and return pointer to corresponding omnibook_operation.
 * Also make corresponding backend initialisation if necessary, and skip
 * to the next entry if it fails.
 */
static struct omnibook_operation *omnibook_backend_match(struct omnibook_tbl *tbl)
{
	int i;
	struct omnibook_operation *matched = NULL;

	for (i = 0; tbl[i].ectypes; i++) {
		if (omnibook_ectype & tbl[i].ectypes) {
			if (tbl[i].io_op.backend->init && tbl[i].io_op.backend->init(&tbl[i].io_op)) {
				dprintk("Backend %s init failed, skipping entry.\n",
					tbl[i].io_op.backend->name);
				continue;
			}
			matched = &tbl[i].io_op;
			dprintk("Returning table entry nr %i.\n", i);
			break;
		}
	}
	return matched;
}

/* 
 * Initialise a feature and add it to the linked list of active features
 */
static int __init omnibook_init(struct omnibook_feature *feature)
{
	int retval = 0;
	mode_t pmode;
	struct proc_dir_entry *proc_entry;
	struct omnibook_operation *op;

	if (!feature)
		return -EINVAL;

/*
 * Select appropriate backend for feature operations
 * We copy the io_op field so the tbl can be initdata
 */
	if (feature->tbl) {
		dprintk("Begin table match of %s feature.\n", feature->name);
		op = omnibook_backend_match(feature->tbl);
		if (!op) {
			dprintk("Match failed: disabling %s.\n", feature->name);
			return -ENODEV;
		}
		feature->io_op = kmalloc(sizeof(struct omnibook_operation), GFP_KERNEL);
		if (!feature->io_op)
			return -ENOMEM;
		memcpy(feature->io_op, op, sizeof(struct omnibook_operation));
	} else
		dprintk("%s feature has no backend table, io_op not initialized.\n", feature->name);

/*
 * Specific feature init code
 */
	if (feature->init && (retval = feature->init(feature->io_op))) {
		printk(O_ERR "Init function of %s failed with error %i.\n", feature->name, retval);
		goto err;
	}
/*
 * procfs file setup
 */
	if (feature->name && feature->read) {
		pmode = S_IFREG | S_IRUGO;
		if (feature->write) {
			pmode |= S_IWUSR;
			if (omnibook_userset)
				pmode |= S_IWUGO;
		}

		proc_entry = create_proc_entry(feature->name, pmode, omnibook_proc_root);

		if (!proc_entry) {
			printk(O_ERR "Unable to create proc entry %s\n", feature->name);
			if (feature->exit)
				feature->exit(feature->io_op);
			retval = -ENOENT;
			goto err;
		}
		proc_entry->data = feature;
		proc_entry->read_proc = &procfile_read_dispatch;
		if (feature->write)
			proc_entry->write_proc = &procfile_write_dispatch;
		#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30)
			proc_entry->owner = THIS_MODULE;
		#endif
	}
	list_add_tail(&feature->list, &omnibook_available_feature->list);
	return 0;
      err:
	if (feature->io_op && feature->io_op->backend->exit)
		feature->io_op->backend->exit(feature->io_op);
	kfree(feature->io_op);
	return retval;
}

/* 
 * Callback function for driver registering :
 * Initialize the linked list of enabled features and call omnibook_init to populate it
 */
static int __init omnibook_probe(struct platform_device *dev)
{
	int i;
	struct omnibook_feature *feature;

	/* temporary hack */
	mutex_init(&kbc_backend.mutex);
	mutex_init(&pio_backend.mutex);
	mutex_init(&ec_backend.mutex);

	omnibook_available_feature = kzalloc(sizeof(struct omnibook_feature), GFP_KERNEL);
	if (!omnibook_available_feature)
		return -ENOMEM;
	INIT_LIST_HEAD(&omnibook_available_feature->list);

	for (i = 0; i < _end_features_driver - _start_features_driver; i++) {

		feature = &_start_features_driver[i];

		if (!feature->enabled)
			continue;

		if ((omnibook_ectype & feature->ectypes) || (!feature->ectypes))
			omnibook_init(feature);
	}

	printk(O_INFO "Enabled features:");
	list_for_each_entry(feature, &omnibook_available_feature->list, list) {
		if (feature->name)
			printk(" %s", feature->name);
	}
	printk(".\n");

	return 0;
}

/* 
 * Callback function for driver removal
 */
static int __exit omnibook_remove(struct platform_device *dev)
{
	struct omnibook_feature *feature, *temp;

	list_for_each_entry_safe(feature, temp, &omnibook_available_feature->list, list) {
		list_del(&feature->list);
		/* Feature specific cleanup */
		if (feature->exit)
			feature->exit(feature->io_op);
		/* Generic backend cleanup */
		if (feature->io_op && feature->io_op->backend->exit)
			feature->io_op->backend->exit(feature->io_op);
		if (feature->name)
			remove_proc_entry(feature->name, omnibook_proc_root);
		kfree(feature->io_op);
	}
	kfree(omnibook_available_feature);

	return 0;
}

/* 
 * Callback function for system suspend 
 */
static int omnibook_suspend(struct platform_device *dev, pm_message_t state)
{
	int retval;
	struct omnibook_feature *feature;

	list_for_each_entry(feature, &omnibook_available_feature->list, list) {
		if (feature->suspend) {
			retval = feature->suspend(feature->io_op);
			if (retval)
				printk(O_ERR "Unable to suspend the %s feature (error %i).\n", feature->name, retval);
		}
	}
	return 0;
}

/* 
 * Callback function for system resume
 */
static int omnibook_resume(struct platform_device *dev)
{
	int retval;
	struct omnibook_feature *feature;

	list_for_each_entry(feature, &omnibook_available_feature->list, list) {
		if (feature->resume) {
			retval = feature->resume(feature->io_op);
			if (retval)
				printk(O_ERR "Unable to resume the %s feature (error %i).\n", feature->name, retval);
		}
	}
	return 0;
}

/* 
 * Find a given available feature by its name
 */
struct omnibook_feature *omnibook_find_feature(char *name)
{
	struct omnibook_feature *feature;

	list_for_each_entry(feature, &omnibook_available_feature->list, list) {
		if (!strcmp(feature->name, name))
			return feature;
	}
	return NULL;
}

/*
 * Maintain compatibility with the old ectype numbers:
 * ex: The user set/get ectype=12 for TSM70=2^(12-1)
 */
static int __init set_ectype_param(const char *val, struct kernel_param *kp)
{
	char *endp;
	int value;

	if (!val)
		return -EINVAL;

	value = simple_strtol(val, &endp, 10);
	if (endp == val)	/* No match */
		return -EINVAL;
	omnibook_ectype = 1 << (value - 1);
	return 0;
}

static int get_ectype_param(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%i", ffs(omnibook_ectype));
}

static int __init omnibook_module_init(void)
{
	int retval;

	printk(O_INFO "Driver version %s.\n", OMNIBOOK_MODULE_VERSION);

	if (omnibook_ectype != NONE)
		printk(O_WARN "Forced load with EC type %i.\n", ffs(omnibook_ectype));
	else if (dmi_check_system(omnibook_ids))
		printk(O_INFO "%s detected.\n", laptop_model);
	else
		printk(O_INFO "Unknown model.\n");

	omnibook_proc_root = proc_mkdir(OMNIBOOK_MODULE_NAME, NULL);
	if (!omnibook_proc_root) {
		printk(O_ERR "Unable to create /proc/%s.\n", OMNIBOOK_MODULE_NAME);
		return -ENOENT;
	}

/*
 * The platform_driver interface was added in linux 2.6.15
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))

	retval = platform_driver_register(&omnibook_driver);
	if (retval < 0)
		return retval;

	omnibook_device = platform_device_alloc(OMNIBOOK_MODULE_NAME, -1);
	if (!omnibook_device) {
		platform_driver_unregister(&omnibook_driver);
		return -ENOMEM;
	}

	retval = platform_device_add(omnibook_device);
	if (retval) {
		platform_device_put(omnibook_device);
		platform_driver_unregister(&omnibook_driver);
		return retval;
	}
#else				/* 2.6.15 */

	retval = driver_register(&omnibook_driver);
	if (retval < 0)
		return retval;

	retval = platform_device_register(&omnibook_device);

	if (retval) {
		driver_unregister(&omnibook_driver);
		return retval;
	}
#endif
	return 0;
}

static void __exit omnibook_module_cleanup(void)
{

/*
 * The platform_driver interface was added in linux 2.6.15
 */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
	platform_device_unregister(omnibook_device);
	platform_driver_unregister(&omnibook_driver);
#else
	platform_device_unregister(&omnibook_device);
	driver_unregister(&omnibook_driver);
#endif

	if (omnibook_proc_root)
		remove_proc_entry("omnibook", NULL);
	printk(O_INFO "Module is unloaded.\n");
}

module_init(omnibook_module_init);
module_exit(omnibook_module_cleanup);

MODULE_AUTHOR("Soós Péter, Mathieu Bérard");
MODULE_VERSION(OMNIBOOK_MODULE_VERSION);
MODULE_DESCRIPTION
    ("Kernel interface for HP OmniBook, HP Pavilion, Toshiba Satellite and Compal ACL00 laptops");
MODULE_LICENSE("GPL");
module_param_call(ectype, set_ectype_param, get_ectype_param, NULL, S_IRUGO);
module_param_named(userset, omnibook_userset, int, S_IRUGO);
MODULE_PARM_DESC(ectype, "Type of embedded controller firmware");
MODULE_PARM_DESC(userset, "Use 0 to disable, 1 to enable users to set parameters");

/* End of file */
