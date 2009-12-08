/*
 * display.c -- External display (LCD,VGA,TV-OUT) feature
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

static const char display_name[][16] = {
	"Internal LCD",
	"External VGA",
	"External TV-OUT",
	"External DVI",
};

static int omnibook_display_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;
	int retval;
	unsigned int sta, en_mask, det_mask;

	retval = backend_display_get(io_op, &sta);
	if (retval < 0)
		return retval;

	for (en_mask = DISPLAY_LCD_ON; en_mask <= DISPLAY_DVI_ON; en_mask = en_mask << 1) {
		det_mask = en_mask << 4;	/* see display masks in omnibook.h */
		if (!(retval & en_mask) && !(retval & det_mask))
			continue;	/* not supported */
		len += sprintf(buffer + len, "%s:", display_name[ffs(en_mask) - 1]);
		if (retval & det_mask)
			len +=
			    sprintf(buffer + len, " display %s",
				    (sta & det_mask) ? "present" : "absent");
		if (retval & en_mask)
			len +=
			    sprintf(buffer + len, " port %s",
				    (sta & en_mask) ? "enabled" : "disabled");
		len += sprintf(buffer + len, "\n");
	}

	return len;
}

static int omnibook_display_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval;
	unsigned int state;
	char *endp;

	state = simple_strtoul(buffer, &endp, 16);
	if (endp == buffer)
		return -EINVAL;
	else
		retval = backend_display_set(io_op, state);

	return retval;
}

static struct omnibook_feature display_driver;

static int __init omnibook_display_init(struct omnibook_operation *io_op)
{
	int retval;
	unsigned int state;
	
	/* Disable file writing if unsuported by backend */
	if (!io_op->backend->display_set)
		display_driver.write = NULL;
		
	retval = backend_display_get(io_op, &state);
	if (retval < 0)
		return retval;
	else
		return 0;
}

static struct omnibook_tbl display_table[] __initdata = {
	{TSM70 | TSX205, {ACPI,}},
	{TSM40, {SMI, SMI_GET_DISPLAY_STATE, SMI_SET_DISPLAY_STATE, 0, 0, 0}},
	{XE3GF | TSP10 | TSM70 | TSM30X | TSM40, SIMPLE_BYTE(EC, XE3GF_STA1, XE3GF_SHDD_MASK)},
	{XE3GC, SIMPLE_BYTE(EC, XE3GC_STA1, XE3GC_CRTI_MASK)},
	{OB500 | OB510 | OB6000 | OB6100 | XE4500, SIMPLE_BYTE(EC, OB500_STA1, OB500_CRTS_MASK)},
	{OB4150, SIMPLE_BYTE(EC, OB4150_STA2, OB4150_CRST_MASK)},
	{0,}
};

static struct omnibook_feature __declared_feature display_driver = {
	.name = "display",
	.enabled = 1,
	.init = omnibook_display_init,
	.read = omnibook_display_read,
	.write = omnibook_display_write,
	.ectypes =
	    XE3GF | XE3GC | OB500 | OB510 | OB6000 | OB6100 | XE4500 | OB4150 | TSP10 | TSM70 | TSM30X |
	    TSM40 | TSX205,
	.tbl = display_table,
};

module_param_named(display, display_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(display, "Use 0 to disable, 1 to enable display status handling");
/* End of file */
