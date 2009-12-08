/*
 * blank.c -- blanking lcd console
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

#include <asm/io.h>
#include "hardware.h"

static struct omnibook_feature blank_driver;

/* 
 * console_blank_hook pointer manipulation is lock protected
 */
extern int (*console_blank_hook) (int);
static DEFINE_SPINLOCK(blank_spinlock);


int omnibook_lcd_blank(int blank)
{
	struct omnibook_feature *blank_feature = omnibook_find_feature("blank");

	if(!blank_feature)
		return -ENODEV;

	return omnibook_apply_write_mask(blank_feature->io_op, blank);
}

static int console_blank_register_hook(void)
{
	spin_lock(&blank_spinlock);
	if (console_blank_hook != omnibook_lcd_blank) {
		if (console_blank_hook == NULL) {
			console_blank_hook = omnibook_lcd_blank;
			printk(O_INFO "LCD backlight turn off at console blanking is enabled.\n");
		} else 
			printk(O_INFO "There is a console blanking solution already registered.\n");
	}
	spin_unlock(&blank_spinlock);
	return 0;
}

static int console_blank_unregister_hook(void)
{
	int retval;
	spin_lock(&blank_spinlock);
	if (console_blank_hook == omnibook_lcd_blank) {
		console_blank_hook = NULL;
		printk(O_INFO "LCD backlight turn off at console blanking is disabled.\n");
	} else if (console_blank_hook) {
		printk(O_WARN "You can not disable another console blanking solution.\n");
		retval = -EBUSY;
	} else {
		printk(O_INFO "Console blanking already disabled.\n");
	}
	spin_unlock(&blank_spinlock);
	return retval;
}

static int omnibook_console_blank_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;

	spin_lock(&blank_spinlock);

	len +=
	    sprintf(buffer + len, "LCD console blanking hook is %s\n",
		    (console_blank_hook == omnibook_lcd_blank) ? "enabled" : "disabled");

	spin_unlock(&blank_spinlock);

	return len;
}

static int omnibook_console_blank_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval;

	switch (*buffer) {
	case '0':
		retval = console_blank_unregister_hook();
		break;
	case '1':
		retval = console_blank_register_hook();
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}

static int __init omnibook_console_blank_init(struct omnibook_operation *io_op)
{	
	return console_blank_register_hook();
}

static void __exit omnibook_console_blank_cleanup(struct omnibook_operation *io_op)
{
	console_blank_unregister_hook();
}

static struct omnibook_tbl blank_table[] __initdata = {
	{TSM70 | TSX205, {CDI, 0, TSM100_BLANK_INDEX, 0, TSM100_LCD_OFF, TSM100_LCD_ON}},
	{XE3GF | XE3GC | AMILOD | TSP10 | TSM70 | TSM30X,
	 COMMAND(KBC, OMNIBOOK_KBC_CMD_LCD_OFF, OMNIBOOK_KBC_CMD_LCD_ON)},
	{OB500 | OB6000 | XE2, {PIO, OB500_GPO1, OB500_GPO1, 0, -OB500_BKLT_MASK, OB500_BKLT_MASK}},
	{OB510 | OB6100, {PIO, OB510_GPO2, OB510_GPO2, 0, -OB510_BKLT_MASK, OB510_BKLT_MASK}},
	{0,}
};

static struct omnibook_feature __declared_feature blank_driver = {
	.name = "blank",
	.enabled = 1,
	.read = omnibook_console_blank_read,
	.write = omnibook_console_blank_write,
	.init = omnibook_console_blank_init,
	.exit = omnibook_console_blank_cleanup,
	.ectypes =
	    XE3GF | XE3GC | OB500 | OB510 | OB6000 | OB6100 | XE2 | AMILOD | TSP10 | TSM70 | TSM30X | TSX205,
	.tbl = blank_table,
};

module_param_named(blank, blank_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(blank, "Use 0 to disable, 1 to enable lcd console blanking");
/* End of file */
