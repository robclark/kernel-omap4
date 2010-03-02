/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef _RTL_DEBUG_H
#define _RTL_DEBUG_H
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#if(LINUX_VERSION_CODE > KERNEL_VERSION(2,6,9))
#include <linux/debugfs.h>
#endif

struct r8192_priv;
struct _tx_desc_8192se;
struct _TX_DESC_8192CE;
struct net_device;

typedef struct _rtl_fs_debug
{
	const char *name;
	struct dentry *dir_drv;
	struct dentry *debug_register;
	u32 hw_type;
	u32 hw_offset; 
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,20))
	bool hw_holding;
#else
	u8 hw_holding;
#endif
} rtl_fs_debug;

void print_buffer(u32 *buffer, int len);
void dump_eprom(struct net_device *dev);
void rtl8192_dump_reg(struct net_device *dev);

#ifdef RTL8192SE
void rtl8192se_dump_tx_desc(struct _tx_desc_8192se *pDesc);
#endif
#ifdef RTL8192CE
void rtl8192ce_dump_tx_desc(struct _TX_DESC_8192CE *pDesc);
#endif

/* debugfs stuff */
#ifdef CONFIG_RTLWIFI_DEBUGFS
int rtl_debug_module_init(struct r8192_priv *priv, const char *name);
void rtl_debug_module_remove(struct r8192_priv *priv);
int rtl_create_debugfs_root(void);
void rtl_remove_debugfs_root(void);
#else
static inline int rtl_debug_module_init(struct r8192_priv *priv, const char *name) {
	return 0;
}
static inline void rtl_debug_module_remove(struct r8192_priv *priv) {
	
}
static inline int rtl_create_debugfs_root(void) {
	return 0;
}
static inline void rtl_remove_debugfs_root(void) {
}
#endif

/* proc stuff */
void rtl8192_proc_init_one(struct net_device *dev);
void rtl8192_proc_remove_one(struct net_device *dev);
void rtl8192_proc_module_init(void);
void rtl8192_proc_module_remove(void);
#endif
