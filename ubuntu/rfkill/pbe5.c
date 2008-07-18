/*******************************************************************************
  
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
  
  Author:
  Pedro Ramalhais <pmr09313@students.fct.unl.pt>
  
  Based on:
  av5100.c from http://ipw2100.sourceforge.net/

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

#define DRV_NAME		"pbe5"
#define DRV_VERSION		"1.3"
#define DRV_DESCRIPTION		"SW RF kill switch for Packard Bell EasyNote E5"
#define DRV_AUTHOR		"Pedro Ramalhais"
#define DRV_LICENSE		"GPL"

static int radio = 1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

MODULE_PARM(radio, "i");

#else /* LINUX_VERSION_CODE < 2.6.0 */

#include <linux/moduleparam.h>
module_param(radio, int, 1);

#endif /* LINUX_VERSION_CODE < 2.6.0 */

MODULE_PARM_DESC(radio, "controls state of radio (1=on, 0=off)");

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_LICENSE(DRV_LICENSE);

/*
 * NOTE: These values were obtained from disassembling the Icon.exe program
 * installed in the Packard Bell EasyNote E5 laptop. The names were guessed,
 * so don't rely on them.
 */
#define PBE5_PORT_TOGGLE	0x0b3
#define PBE5_VALUE_TOGGLE_ON	0x01
#define PBE5_VALUE_TOGGLE_OFF	0x00
#define PBE5_PORT_APPLY		0x0b2
#define PBE5_VALUE_APPLY	0xef

// Some "booleans" =;-)
#define PBE5_RADIO_OFF	0
#define PBE5_RADIO_ON	1

static int pbe5_radio_status = PBE5_RADIO_ON;

unsigned char pbe5_get_radio(void)
{
	unsigned char val = 0x00;

	val = inb(PBE5_PORT_TOGGLE);

	return val;
}

static void pbe5_set_radio(int state_set)
{
	pbe5_radio_status = pbe5_get_radio();

	if (pbe5_radio_status != state_set) {
		// Set the radio toggle register
		outb(PBE5_VALUE_TOGGLE_ON, PBE5_PORT_TOGGLE);
		// Commit the radio toggle register value
		outb(PBE5_VALUE_APPLY, PBE5_PORT_APPLY);
		// Update the radio status
		pbe5_radio_status = pbe5_get_radio();

		printk(KERN_INFO DRV_NAME ": Radio turned %s\n",
			(state_set  == PBE5_RADIO_ON) ? "ON" : "OFF");
	} else {
		printk(KERN_INFO DRV_NAME ": Radio already %s\n",
			(state_set  == PBE5_RADIO_ON) ? "ON" : "OFF");
	}
}


/*
 * proc stuff
 */
static struct proc_dir_entry *dir_base = NULL;

static int proc_set_radio(struct file *file, const char *buffer, 
			  unsigned long count, void *data)
{
	pbe5_set_radio(buffer[0] == '0' ? PBE5_RADIO_OFF : PBE5_RADIO_ON);

	return count;
}

static int proc_get_radio(char *page, char **start, off_t offset,
			  int count, int *eof, void *data)
{
	int len = 0;

	len += snprintf(page, count, DRV_NAME ": %d\n", 
			pbe5_radio_status == PBE5_RADIO_OFF ? 0 : 1);

	*eof = 1;
	return len;
}


static void pbe5_proc_cleanup(void)
{
	if (dir_base) {
		remove_proc_entry("radio", dir_base);
		remove_proc_entry(DRV_NAME, NULL);
		dir_base = NULL;
	}
}


static int pbe5_proc_init(void)
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
	pbe5_proc_cleanup();
	return err;
}

/*
 * module stuff
 */
static int __init pbe5_init(void)
{
	pbe5_proc_init();

	pbe5_set_radio((radio == 1) ? PBE5_RADIO_ON : PBE5_RADIO_OFF);

	return 0;
}

static void __exit pbe5_exit(void)
{
	pbe5_set_radio(PBE5_RADIO_OFF);

	pbe5_proc_cleanup();
}

module_init(pbe5_init);
module_exit(pbe5_exit);

/*
         1         2         3         4         5         6         7
12345678901234567890123456789012345678901234567890123456789012345678901234567890
*/
