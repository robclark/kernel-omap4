/*
 * fan.c -- fan status/control
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

#include <linux/delay.h>
#include <asm/io.h>
#include "hardware.h"

static const struct omnibook_operation ctmp_io_op = { EC, XE3GF_CTMP, 0, 0, 0, 0 };
static const struct omnibook_operation fot_io_op = { EC, XE3GF_FOT, XE3GF_FOT, 0, 0, 0 };

static int omnibook_get_fan(struct omnibook_operation *io_op)
{
	u8 fan;
	int retval;

	if ((retval = backend_byte_read(io_op, &fan)))
		return retval;

	/*
	 * For most models the reading is a bool
	 * It as to be inverted on all but OB6000|OB6100|OB4150|AMILOD
	 * TSP10|XE3GF|TSX205 return an integer
	 */

	if (omnibook_ectype & (TSP10 | XE3GF | TSX205))
		retval = fan;
	else if (omnibook_ectype & (OB6000 | OB6100 | OB4150 | AMILOD))
		retval = !!fan;
	else
		retval = !fan;

	return retval;
}

static int omnibook_fan_on(struct omnibook_operation *io_op)
{
	return omnibook_apply_write_mask(io_op, 1);
}

static int omnibook_fan_off(struct omnibook_operation *io_op)
{
	int i, retval = 0;

	if (!(omnibook_ectype & (XE3GF | TSP10 | TSX205))) {
		retval = omnibook_apply_write_mask(io_op, 0);
		return retval;
	} else {
	/*
 	 * Special handling for XE3GF & TSP10
	 */
		u8 fot, temp, fan;

		if(mutex_lock_interruptible(&io_op->backend->mutex))
			return -ERESTARTSYS;	

		retval = __backend_byte_read(io_op, &fan);

		/* error or fan is already off */
		if (retval || !fan)
			goto out;

		/* now we set FOT to current temp, then reset to initial value */
		if ((retval = __backend_byte_read(&fot_io_op, &fot)))
			goto out;
		if ((retval = __backend_byte_read(&ctmp_io_op, &temp)))
			goto out;

		/* Wait for no longer than 250ms (this is arbitrary). */
		for (i = 0; i < 250; i++) {
			__backend_byte_write(&fot_io_op, temp);
			mdelay(1);
			__backend_byte_read(io_op, &fan);
			if (!fan) /* Fan is off */
				break;
		}
		__backend_byte_write(&fot_io_op, fot);

		if(i == 250 ) {
			printk(O_ERR "Attempt to switch off the fan failed.\n");
			retval = -EIO;
		}

		out:		
		mutex_unlock(&io_op->backend->mutex);
	}
		

	return retval;
}

static int omnibook_fan_read(char *buffer, struct omnibook_operation *io_op)
{
	int fan;
	int len = 0;
	char *str;

	fan = omnibook_get_fan(io_op);
	if (fan < 0)
		return fan;
	str = (fan) ? "on" : "off";

	if (fan > 1)
		len += sprintf(buffer + len, "Fan is %s (level %d)\n", str, fan);
	else
		len += sprintf(buffer + len, "Fan is %s\n", str);

	return len;
}

static int omnibook_fan_write(char *buffer, struct omnibook_operation *io_op)
{
	int retval;

	switch (*buffer) {
	case '0':
		retval = omnibook_fan_off(io_op);
		break;
	case '1':
		retval = omnibook_fan_on(io_op);
		break;
	default:
		retval = -EINVAL;
	}
	return retval;
}

static struct omnibook_feature fan_driver;

static int __init omnibook_fan_init(struct omnibook_operation *io_op)
{
	/*
	 * OB4150
	 * XE2
	 * AMILOD
	 * They only support fan reading 
	 */
	if (omnibook_ectype & (OB4150 | XE2 | AMILOD))
		fan_driver.write = NULL;
	return 0;
}

static struct omnibook_tbl fan_table[] __initdata = {
	{XE3GF | TSP10 | TSM70 | TSX205, {EC, XE3GF_FSRD, XE3GF_FSRD, 0, XE3GF_FAN_ON_MASK, 0}},
	{OB500,
	 {PIO, OB500_GPO1, OB500_GPO1, OB500_FAN_OFF_MASK, -OB500_FAN_ON_MASK, OB500_FAN_OFF_MASK}},
	{OB510,
	 {PIO, OB510_GPIO, OB510_GPIO, OB510_FAN_OFF_MASK, -OB510_FAN_ON_MASK, OB510_FAN_OFF_MASK}},
	{OB6000 | OB6100,
	 {EC, OB6000_STA1, OB6000_STA1, OB6000_FAN_MASK, OB6000_FAN_MASK, -OB6000_FAN_MASK}},
	{OB4150 | AMILOD, {EC, OB4150_STA1, 0, OB4150_FAN_MASK, 0, 0}},
	{XE2, {PIO, OB500_GPO1, 0, XE2_FAN_MASK, 0, 0}},
	{0,}
};

static struct omnibook_feature __declared_feature fan_driver = {
	.name = "fan",
	.enabled = 1,
	.read = omnibook_fan_read,
	.write = omnibook_fan_write,
	.init = omnibook_fan_init,
	.ectypes = XE3GF | OB500 | OB510 | OB6000 | OB6100 | OB4150 | XE2 | AMILOD | TSP10 | TSX205,
	.tbl = fan_table,
};

module_param_named(fan, fan_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(fan, "Use 0 to disable, 1 to enable fan status monitor and control");
/* End of file */
