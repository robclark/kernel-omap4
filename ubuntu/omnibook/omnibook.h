/*
 * omnibook.h -- High level data structures and functions of omnibook
 *               support code
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
 * Modified by Mathieu Bérard <mathieu.berard@crans.org>, 2006-2007
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/input.h>
#include <linux/version.h>

/*
 * EC types
 */

extern enum omnibook_ectype_t {
	NONE   = 0,	  /* 0  Default/unknown EC type */ 
	XE3GF  = (1<<0),  /* 1  HP OmniBook XE3 GF, most old Toshiba Satellites */
	XE3GC  = (1<<1),  /* 2  HP OmniBook XE3 GC, GD, GE and compatible */
	OB500  = (1<<2),  /* 3  HP OmniBook 500 and compatible */
	OB510  = (1<<3),  /* 4  HP OmniBook 510 */
	OB6000 = (1<<4),  /* 5  HP OmniBook 6000 */
	OB6100 = (1<<5),  /* 6  HP OmniBook 6100 */
	XE4500 = (1<<6),  /* 7  HP OmniBook xe4500 and compatible */
	OB4150 = (1<<7),  /* 8  HP OmniBook 4150 */
	XE2    = (1<<8),  /* 9  HP OmniBook XE2 */
	AMILOD = (1<<9),  /* 10 Fujitsu Amilo D */
	TSP10  = (1<<10), /* 11 Toshiba Satellite P10, P15, P20 and compatible */
	TSM70  = (1<<11), /* 12 Toshiba Satellite M40X, M70 and compatible */
	TSM40  = (1<<12), /* 13 Toshiba Satellite M40, M45 and Tecra S1 */
	TSA105 = (1<<13), /* 14 Toshiba Satellite A105 and compatible (Real support is MISSING) */
	TSM30X = (1<<14), /* 15 Toshiba Stallite M30X and compatible */
	TSX205 = (1<<15)  /* 16 Toshiba Stallite X205 and compatible */
} omnibook_ectype;

#define ALL_ECTYPES XE3GF|XE3GC|OB500|OB510|OB6000|OB6100|XE4500|OB4150|XE2|AMILOD|TSP10|TSM70|TSM40|TSA105|TSM30X|TSX205

/*
 * This represent a feature provided by this module
 */

struct omnibook_operation;

struct omnibook_feature {
	char *name;						/* Name */
	int enabled;						/* Set from module parameter */
	int (*read) (char *,struct omnibook_operation *);	/* Procfile read function */
	int (*write) (char *,struct omnibook_operation *);	/* Procfile write function */
	int (*init) (struct omnibook_operation *);		/* Specific Initialization function */
	void (*exit) (struct omnibook_operation *);		/* Specific Cleanup function */
	int (*suspend) (struct omnibook_operation *);		/* PM Suspend function */
	int (*resume) (struct omnibook_operation *);		/* PM Resume function */
	int ectypes;						/* Type(s) of EC we support for this feature (bitmask) */
	struct omnibook_tbl *tbl;
	struct omnibook_operation *io_op;
	struct list_head list;
};

/*
 * State of a Wifi/Bluetooth adapter
 */
enum {
	WIFI_EX = (1<<0),	/* 1 1=present 0=absent */
	WIFI_STA = (1<<1),	/* 2 1=enabled 0=disabled */
	KILLSWITCH = (1<<2),	/* 4 1=radio on 0=radio off */
	BT_EX = (1<<3),		/* 8 1=present 0=absent */
	BT_STA = (1<<4),	/* 16 1=enabled 0=disabled */
};

/*
 * Hotkeys state backend neutral masks
 */
enum {
	HKEY_ONETOUCH = (1<<0),		/* 1   Ontetouch button scancode generation */
	HKEY_MULTIMEDIA = (1<<1),	/* 2   "Multimedia hotkeys" scancode generation */	
	HKEY_FN = (1<<2),		/* 4   Fn + foo hotkeys scancode generation */
	HKEY_STICK = (1<<3),		/* 8   Stick key (Fn locked/unlocked on keypress)  */
	HKEY_TWICE_LOCK = (1<<4),	/* 16  Press Fn twice to lock */
	HKEY_DOCK = (1<<5),		/* 32  (Un)Dock events scancode generation */
	HKEY_FNF5 = (1<<6),		/* 64  Fn + F5 (toggle display) is enabled */
};

#define HKEY_LAST_SHIFT 6

/*
 * Display state backend neutral masks
 * _ON masks = port is powered up and running
 * _DET masks = a plugged display have been detected 
 */

enum {	
	DISPLAY_LCD_ON = (1<<0),	/* 1 Internal LCD panel */
	DISPLAY_CRT_ON = (1<<1),	/* 2 External VGA port */
	DISPLAY_TVO_ON = (1<<2),	/* 4 External TV-OUT port */
	DISPLAY_DVI_ON = (1<<3),	/* 8 External DVI port */
	DISPLAY_LCD_DET = (1<<4),	/* 16 Internal LCD panel */
	DISPLAY_CRT_DET = (1<<5),	/* 32 External VGA port */
	DISPLAY_TVO_DET = (1<<6),	/* 64 External TV-OUT port */
	DISPLAY_DVI_DET = (1<<7),	/* 128 External DVI port */
};

extern unsigned int omnibook_max_brightness;
int set_omnibook_param(const char *val, struct kernel_param *kp);
int omnibook_lcd_blank(int blank);
struct omnibook_feature *omnibook_find_feature(char *name);
void omnibook_report_key(struct input_dev *dev, unsigned int keycode);

/* 
 * __attribute_used__ is not defined anymore in 2.6.24
 * but __used appeared only in 2.6.22
 */
#ifndef __used
#define __used	__attribute_used__
#endif

#define __declared_feature __attribute__ (( __section__(".features"),  __aligned__(__alignof__ (struct omnibook_feature)))) __used

/*
 * yet another printk wrapper
 */
#define O_INFO	KERN_INFO OMNIBOOK_MODULE_NAME ": "
#define O_WARN	KERN_WARNING OMNIBOOK_MODULE_NAME ": "
#define O_ERR	KERN_ERR OMNIBOOK_MODULE_NAME ": "

#ifdef CONFIG_OMNIBOOK_DEBUG
#define dprintk(fmt, args...) printk(KERN_INFO "%s: " fmt, OMNIBOOK_MODULE_NAME, ## args)
#define dprintk_simple(fmt, args...) printk(fmt, ## args)
#else
#define dprintk(fmt, args...)	do { } while(0)
#define dprintk_simple(fmt, args...) do { } while(0)
#endif

/* End of file */
