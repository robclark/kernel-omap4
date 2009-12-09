/*******************************************************************************

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

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
  Marcel Naziri <fsam7400@zwobbl.de>

  Based on:
  pbe5.c by Pedro Ramalhais <pmr09313@students.fct.unl.pt>

  Many thanks to:
  Pedro Ramalhais for spending several nights with me on IRC

*******************************************************************************/

#ifdef CONFIG_X86
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/moduleparam.h>
#else
#include <linux/modversions.h>
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/kmod.h>
#include <linux/io.h>
#include <asm/uaccess.h>

#define DRV_NAME         "fsam7400"
#define DRV_VERSION      "0.5.2"
#define DRV_DESCRIPTION  "SW RF kill switch for Fujitsu Siemens Amilo M 7400 / Maxdata 7000DX"
#define DRV_COPYRIGHT    "Copyright(c) 2004 zwobbl"
#define DRV_AUTHOR       "Marcel Naziri"
#define DRV_LICENSE      "GPL"

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_LICENSE(DRV_LICENSE);

#define RADIO_NONE     0xFFFFFFFF
#define RADIO_OFF      0x00000000
#define RADIO_ON       0x00000010

static int radio = RADIO_NONE;
static int autooff = 1;
static int autoload = 0;
static int uid = 0;
static int gid = 0;
static int debug = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param(radio, int, 0444);
module_param(autooff, int, 0444);
module_param(autoload, int, 0444);
module_param(uid, int, 0444);
module_param(gid, int, 0444);
module_param(debug, int, 0444);
#else
MODULE_PARM(radio, "i");
MODULE_PARM(autooff, "i");
MODULE_PARM(autoload, "i");
MODULE_PARM(uid, "i");
MODULE_PARM(gid, "i");
MODULE_PARM(debug, "i");
#endif

MODULE_PARM_DESC(radio, "desired radio state when loading module");
MODULE_PARM_DESC(autooff, "turns radio off when unloading module (default)");
MODULE_PARM_DESC(autoload, "load/unloads ipw2100 driver when toggling radio");
MODULE_PARM_DESC(uid, "user ID for proc entry");
MODULE_PARM_DESC(gid, "group ID for proc entry");
MODULE_PARM_DESC(debug, "enables debug output on syslog");

/* some more or less useful macros */

#define DEBUG if (debug)
#define DEBUG_OUT0(a) DEBUG printk(KERN_INFO DRV_NAME ": " a)
#define DEBUG_OUT1(a,b) DEBUG printk(KERN_INFO DRV_NAME ": " a,b)
#define DEBUG_OUT2(a,b,c) DEBUG printk(KERN_INFO DRV_NAME ": " a,b,c)
#define DEBUG_OUT3(a,b,c,d) DEBUG printk(KERN_INFO DRV_NAME ": " a,b,c,d)

#define ONOFF(x) (x) ? "ON" : "OFF"
#define RADIO_ONOFF(x) (x) == RADIO_ON ? "ON" : "OFF"
#define TOUL(x) (unsigned long) (x)

/*
 * NOTE: These values were obtained from disassembling the wbutton.sys driver
 * installed in the Fujitsu Siemens Amilo M 7400 laptop. The names were guessed,
 * so don't rely on them.
 */

/*** hardware dependant stuff ***/

#define BIOS_CODE_ADDR       0x000F0000
#define BIOS_CODE_ALT_MASK   0xFFFFC000

#define BIOS_CODE_MAPSIZE      0x010000
#define BIOS_CODE_ALT_MAPSIZE  0x004000

#define BIOS_MAGIC_COMMAND    0x9610
#define BIOS_MAGIC_OFF        0x0035
#define BIOS_MAGIC_ON         0x0135
#define BIOS_MAGIC_CHECK      0x0235

#define PTR_POSITION  5
#define ALLIGNED_STEP 0x10

#define BIOS_SIGN_SIZE 4
static const char bios_sign[] = {
   0x42, 0x21, 0x55, 0x30
};

#define WLAN_DISABLED_IN_BIOS  0x01
#define WLAN_ENABLED_IN_BIOS   0x03

static unsigned long bios_code = 0;

static int fsam_bios_routine(int eax, int ebx)
{
   __asm__ __volatile__(
      "call *%3 \t\n"
      : "=a"(eax)
      : "a"(eax), "b"(ebx), "c"(bios_code)
      );
   return (eax & 0xFF);
}

static int fsam_call_bios(int value)
{
   if (bios_code) {
      int command = BIOS_MAGIC_COMMAND;
      DEBUG_OUT2("bios routine gets parameter eax=%X and ebx=%X\n",
                  command, value);
      value = fsam_bios_routine(command, value);
      DEBUG_OUT1("bios routine results %X\n", value);
      return value;
   }
   return ~0;
}

/* pointer to mapped memory*/
static void *mem_code = NULL;

static inline void fsam_unmap_memory(void)
{
   bios_code = 0;
   if (mem_code) {
      iounmap(mem_code);
   }
}

static inline int fsam_map_memory(void)
{
   const unsigned long max_offset = BIOS_CODE_MAPSIZE - BIOS_SIGN_SIZE - PTR_POSITION;
   unsigned long offset;
   unsigned int addr;
   mem_code = ioremap(BIOS_CODE_ADDR, BIOS_CODE_MAPSIZE);
   if (!mem_code)
      goto fail;
   DEBUG_OUT3("physical memory %x-%x mapped to virtual address %p\n",
              BIOS_CODE_ADDR, BIOS_CODE_ADDR+BIOS_CODE_MAPSIZE, mem_code);
   for ( offset = 0; offset < max_offset; offset += ALLIGNED_STEP )
      if (check_signature((void*)TOUL(mem_code) + offset, bios_sign, BIOS_SIGN_SIZE))
         break;
   if (offset >= max_offset)
     goto fail;
   DEBUG_OUT1("bios signature found at offset %lx\n", offset);
   addr = readl((void*)TOUL(mem_code) + offset + PTR_POSITION);
   if (addr < BIOS_CODE_ADDR) {
      DEBUG_OUT0("bios routine out of memory range, "
                 "doing some new memory mapping...\n");
      iounmap(mem_code);
      mem_code = NULL;
      addr &= BIOS_CODE_ALT_MASK;
      mem_code = ioremap(addr, BIOS_CODE_ALT_MAPSIZE);
      if (!mem_code)
         goto fail;
      DEBUG_OUT3("physical memory %x-%x mapped to virtual address %p\n",
                 addr, addr+BIOS_CODE_ALT_MAPSIZE, mem_code);
      addr &= 0x3FFF;
   } else
     addr &= 0xFFFF;

   bios_code = addr + TOUL(mem_code);
   DEBUG_OUT1("supposed address of bios routine is %lx\n", bios_code);
   return 1;
 fail:
   fsam_unmap_memory();
   return 0;
}

/*** ipw2100 loading ***/

static inline void do_ipw2100_loading(int state) 
{
  int status;
  char *mode;
  char *envp[] = { "HOME=/",
                   "TERM=linux",
                   "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
  if (state == RADIO_ON) {
    char *argv[] = { "/sbin/modprobe", "-s", "-k", "ipw2100", NULL };
    mode = "loading";
    status = call_usermodehelper(argv[0], argv, envp, 1);
  } else {
    char *argv[] = { "/sbin/rmmod", "ipw2100", NULL };
    mode = "removing";
    status = call_usermodehelper(argv[0], argv, envp, 1);
  }
  DEBUG_OUT2("%s of ipw2100 module %s\n", mode, status == 0 ? "successful" : "FAILED");
}

/*** interface stuff ***/

static void rfkill_set_radio(int value)
{
   radio = value == RADIO_ON ? fsam_call_bios(BIOS_MAGIC_ON) :
                               fsam_call_bios(BIOS_MAGIC_OFF);
   if (autoload) do_ipw2100_loading(radio);
}

static inline int rfkill_get_radio(void)
{
   return radio;
}

static inline int rfkill_supported(void)
{
   return bios_code != 0;
}

static inline void rfkill_initialize(void) {
   fsam_map_memory();
   if (rfkill_supported()) {
      radio = radio != RADIO_NONE
              ? ( radio ? RADIO_ON : RADIO_OFF ) /*module parameter*/
              : ( fsam_call_bios(BIOS_MAGIC_CHECK) == WLAN_ENABLED_IN_BIOS
                  ? RADIO_ON : RADIO_OFF );
   }
}

static inline void rfkill_uninitialize(void) {
   fsam_unmap_memory();
}

/*** proc stuff ***/

static inline int common_proc_set_radio(struct file *file, const char *buffer, 
                                        unsigned long count, void *data)
{
   unsigned long len = 7;
   char newstate[len];
   len = count < len ? count : len;
   if ( copy_from_user(newstate, buffer, len) != 0 )
     return -EFAULT;
   if ( (*newstate == '1' || *newstate == '0') &&
        (count == 1 || isspace(newstate[1])) )
     rfkill_set_radio(*newstate == '1' ? RADIO_ON : RADIO_OFF);
   else
   if ( !strncmp(newstate, "on", 2)  &&
        (count == 2 || isspace(newstate[2])) )
     rfkill_set_radio(RADIO_ON);
   else
   if ( !strncmp(newstate, "off", 3) &&
        (count == 3 || isspace(newstate[3])) )
     rfkill_set_radio(RADIO_OFF);
   else
   if ( !strncmp(newstate, "resume", 6) &&
        (count == 6 || isspace(newstate[6])) )
     rfkill_set_radio(radio);
   return count;
}

static inline int common_proc_get_radio(char *page, char **start, off_t offset,
                                        int count, int *eof, void *data)
{
   int len = snprintf(page, count, DRV_DESCRIPTION ", v" DRV_VERSION "\n"
                                   "  auto-off is %s, auto-load is %s\n",
                                   ONOFF(autooff), ONOFF(autoload));
   len += snprintf(page+len, count-len, "  radio state is %s\n",
                                        RADIO_ONOFF(rfkill_get_radio()));
   *eof = 1;
   return len;
}

#define PROC_DIR    "driver/wireless"
#define PROC_RADIO  "radio"

static struct proc_dir_entry *dir_base = NULL;

static inline void common_proc_cleanup(void)
{
   if (dir_base) {
      remove_proc_entry(PROC_RADIO, dir_base);
      remove_proc_entry(PROC_DIR, NULL);
      dir_base = NULL;
   }
}

static inline int common_proc_init(void)
{
   struct proc_dir_entry *ent;
   int err = 0;
   dir_base = proc_mkdir(PROC_DIR, NULL);
   if (dir_base == NULL) {
      printk(KERN_ERR DRV_NAME ": Unable to initialize /proc/" PROC_DIR "\n");
      err = -ENOMEM;
      goto fail;
   }
   ent = create_proc_entry(PROC_RADIO,
                           S_IFREG | S_IRUGO | S_IWUSR | S_IWGRP,
                           dir_base);
   if (ent) {
      ent->uid = uid;
      ent->gid = gid;
      ent->read_proc = common_proc_get_radio;
      ent->write_proc = common_proc_set_radio;
   } else {
      printk(KERN_ERR DRV_NAME ": Unable to initialize /proc/"
                      PROC_DIR "/" PROC_RADIO "\n");
      err = -ENOMEM;
      goto fail;
   }
   return 0;
 fail:
   common_proc_cleanup();
   return err;
}

/*** module stuff ***/

static int __init common_init(void)
{ 
   printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", v" DRV_VERSION "\n");
   printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");
   rfkill_initialize();
   if (rfkill_supported()) {
      common_proc_init();
      if (radio != RADIO_NONE)
         rfkill_set_radio(radio);
   } else
      printk(KERN_INFO DRV_NAME ": no supported wireless hardware found\n");
   return 0;
}

static void __exit common_exit(void)
{
   if (rfkill_supported() && autooff)
      rfkill_set_radio(RADIO_OFF);
   common_proc_cleanup();
   rfkill_uninitialize();
   printk(KERN_INFO DRV_NAME ": module removed successfully\n");
}

module_init(common_init);
module_exit(common_exit);

#else
#error This driver is only available for X86 architecture
#endif

/*
         1         2         3         4         5         6         7
12345678901234567890123456789012345678901234567890123456789012345678901234567890
*/
