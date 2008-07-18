/*******************************************************************************
  
  Copyright(c) 2003 - 2004 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of version 2 of the GNU General Public License as 
  published by the Free Software Foundation.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <asm/io.h>


#define DRV_NAME		"av5100"
#define DRV_VERSION		"1.3"
#define DRV_DESCRIPTION		"SW RF kill switch for Averatec 5100P"
#define DRV_COPYRIGHT		"Copyright(c) 2003-2004 Intel Corporation"

static int radio = 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

MODULE_PARM(radio, "i");

#else /* LINUX_VERSION_CODE < 2.6.0 */

#include <linux/moduleparam.h>
module_param(radio, int, 1);

#endif /* LINUX_VERSION_CODE < 2.6.0 */

MODULE_PARM_DESC(radio, "controls state of radio (1=on, 0=off)");

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

#define AV5100_RADIO_ON (0xe0)
#define AV5100_RADIO_OFF (0xe1)

static int av5100_radio = AV5100_RADIO_OFF;

static void av5100_set_radio(int state)
{
	printk(KERN_INFO DRV_NAME ": Radio being turned %s\n",
	       (state  == AV5100_RADIO_ON) ? "ON" : "OFF");
	outl(0x80020800, 0xcf8);
	outb(0x6f, 0x0072);
	outl(0x1800ffff, 0x1184); 
	outb(state, 0x00b2);
	av5100_radio = state;
}


/*
 * proc stuff
 */
static struct proc_dir_entry *dir_base = NULL;

static int proc_set_radio(struct file *file, const char *buffer, 
			  unsigned long count, void *data)
{
	av5100_set_radio(buffer[0] == '0' ? AV5100_RADIO_OFF : AV5100_RADIO_ON);
	
	return count;
}

static int proc_get_radio(char *page, char **start, off_t offset,
			  int count, int *eof, void *data)
{
	int len = 0;
	
	len += snprintf(page, count, DRV_NAME ": %d\n", 
			av5100_radio == AV5100_RADIO_OFF ? 0 : 1);
	
	*eof = 1;
	return len;
}


static void av5100_proc_cleanup(void)
{
	if (dir_base) {
		remove_proc_entry("radio", dir_base);
		remove_proc_entry(DRV_NAME, NULL);
		dir_base = NULL;
	}
}


static int av5100_proc_init(void)
{
	struct proc_dir_entry *ent;
	int err = 0;

	dir_base = create_proc_entry(DRV_NAME, S_IFDIR, NULL);
	if (dir_base == NULL) {
		printk(KERN_ERR DRV_NAME ": Unable to initialise /proc/" 
		       DRV_NAME "\n");
		err = -ENOMEM;
		goto fail;
	}


	ent = create_proc_entry("radio", S_IFREG | S_IRUGO | S_IWUSR, 
				dir_base);
	if (ent) {
		ent->read_proc = proc_get_radio;
		ent->write_proc = proc_set_radio;
	} else {
		printk(KERN_ERR
		       "Unable to initialize /proc/" DRV_NAME "/radio\n");
		err = -ENOMEM;
		goto fail;
	}

	return 0;

 fail:
	av5100_proc_cleanup();
	return err;
}

/*
 * module stuff
 */
static int __init av5100_init(void)
{
	av5100_proc_init();

	av5100_set_radio((radio == 1) ? AV5100_RADIO_ON : AV5100_RADIO_OFF);
	
	return 0;
}

static void __exit av5100_exit(void)
{
	av5100_set_radio(AV5100_RADIO_OFF);

	av5100_proc_cleanup();
}

module_init(av5100_init);
module_exit(av5100_exit);

/*
         1         2         3         4         5         6         7
12345678901234567890123456789012345678901234567890123456789012345678901234567890
*/
