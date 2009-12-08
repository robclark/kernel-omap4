/*
 * fan_policy.c -- fan policy support
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

#include <linux/ctype.h>
#include "hardware.h"

/*
 * Default temperature limits.
 * Danger! You may overheat your CPU!
 * Do not change these values unless you exactly know what you do.
 */

#define OMNIBOOK_FAN_LEVELS			8
#define OMNIBOOK_FAN_MIN			25	/* Minimal value of fan off temperature */
#define OMNIBOOK_FOT_MAX			75	/* Maximal value of fan off temperature */
#define OMNIBOOK_FAN_MAX			95	/* Maximal value of fan on temperature */
#define OMNIBOOK_FOT_DEFAULT			60	/* Default value of fan off temperature */
#define OMNIBOOK_FAN1_DEFAULT			75	/* Default value of fan on temperature */
#define OMNIBOOK_FAN2_DEFAULT			85	/* Default value of fan level 2 temperature */
#define OMNIBOOK_FAN3_DEFAULT			90	/* Default value of fan level 3 temperature */
#define OMNIBOOK_FAN4_DEFAULT			95	/* Default value of fan level 4 temperature */
#define OMNIBOOK_FAN5_DEFAULT			95	/* Default value of fan level 5 temperature */
#define OMNIBOOK_FAN6_DEFAULT			95	/* Default value of fan level 6 temperature */
#define OMNIBOOK_FAN7_DEFAULT			95	/* Default value of fan level 7 temperature */

static const u8 fan_defaults[] = {
		OMNIBOOK_FOT_DEFAULT,
		OMNIBOOK_FAN1_DEFAULT,
		OMNIBOOK_FAN2_DEFAULT,
		OMNIBOOK_FAN3_DEFAULT,
		OMNIBOOK_FAN4_DEFAULT,
		OMNIBOOK_FAN5_DEFAULT,
		OMNIBOOK_FAN6_DEFAULT,
		OMNIBOOK_FAN7_DEFAULT,
};

static int omnibook_get_fan_policy(struct omnibook_operation *io_op, u8 *fan_policy)
{
	int retval ;
	int i;

	for (i = 0; i < OMNIBOOK_FAN_LEVELS; i++) {
		io_op->read_addr = XE3GF_FOT + i;
		if ((retval = __backend_byte_read(io_op, &fan_policy[i])))
			return retval;
	}

	return 0;
}

static int omnibook_set_fan_policy(struct omnibook_operation *io_op, const u8 *fan_policy)
{
	int retval;
	int i;

	if (fan_policy[0] > OMNIBOOK_FOT_MAX)
		return -EINVAL;

	for (i = 0; i < OMNIBOOK_FAN_LEVELS; i++) {
		if ((fan_policy[i] > fan_policy[i + 1])
		    || (fan_policy[i] < OMNIBOOK_FAN_MIN)
		    || (fan_policy[i] > OMNIBOOK_FAN_MAX))
			return -EINVAL;
	}
	for (i = 0; i < OMNIBOOK_FAN_LEVELS; i++) {
		io_op->write_addr = XE3GF_FOT + i;
		if ((retval = __backend_byte_write(io_op, fan_policy[i])))
			return retval;
	}

	return 0;
}

static int omnibook_fan_policy_read(char *buffer, struct omnibook_operation *io_op)
{
	int retval;
	int len = 0;
	u8 i;
	u8 fan_policy[OMNIBOOK_FAN_LEVELS];

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;

	retval = omnibook_get_fan_policy(io_op, &fan_policy[0]);

	mutex_unlock(&io_op->backend->mutex);

	if(retval)
		return retval;

	len += sprintf(buffer + len, "Fan off temperature:        %2d C\n", fan_policy[0]);
	len += sprintf(buffer + len, "Fan on temperature:         %2d C\n", fan_policy[1]);
	for (i = 2; i < OMNIBOOK_FAN_LEVELS; i++) {
		len +=
		    sprintf(buffer + len, "Fan level %1d temperature:    %2d C\n", i,
			    fan_policy[i]);
	}
	len += sprintf(buffer + len, "Minimal temperature to set: %2d C\n", OMNIBOOK_FAN_MIN);
	len += sprintf(buffer + len, "Maximal temperature to set: %2d C\n", OMNIBOOK_FAN_MAX);

	return len;
}

static int omnibook_fan_policy_write(char *buffer, struct omnibook_operation *io_op)
{
	int n = 0;
	char *b;
	char *endp;
	int retval;
	int temp;
	u8 fan_policy[OMNIBOOK_FAN_LEVELS];

	if(mutex_lock_interruptible(&io_op->backend->mutex))
		return -ERESTARTSYS;

	if ((retval = omnibook_get_fan_policy(io_op, &fan_policy[0])))
		goto out;

	/* 
	 * Could also be done much simpler using sscanf(,"%u %u ... 
	 * but this would hardcode OMNIBOOK_FAN_LEVELS.
	 * The parsed format is "%u " repeated OMNIBOOK_FAN_LEVELS+1 times
	 */

	b = buffer;
	do {
		dprintk("n=[%i] b=[%s]\n", n, b);
		if (n > OMNIBOOK_FAN_LEVELS) {
			retval = -EINVAL;
			goto out;
		}
		if (!isspace(*b)) {
			temp = simple_strtoul(b, &endp, 10);
			if (endp != b) {	/* there was a match */
				fan_policy[n++] = temp;
				b = endp;
			} else {
				retval = -EINVAL;
				goto out;
			}
		} else
			b++;
	} while ((*b != '\0') && (*b != '\n'));

	/* A zero value set the defaults */
	if ((fan_policy[0] == 0) && (n == 1))
		retval = omnibook_set_fan_policy(io_op, &fan_defaults[0]);
	else
		retval = omnibook_set_fan_policy(io_op, &fan_policy[0]);

	out:
	mutex_unlock(&io_op->backend->mutex);
	return retval;
}

static struct omnibook_tbl fan_policy_table[] __initdata = {
	{XE3GF, {EC,}},
	{0,}
};

static struct omnibook_feature __declared_feature fan_policy_driver = {
	.name = "fan_policy",
	.enabled = 1,
	.read = omnibook_fan_policy_read,
	.write = omnibook_fan_policy_write,
	.ectypes = XE3GF,
	.tbl = fan_policy_table,
};

module_param_named(fan_policy, fan_policy_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(fan_policy, "Use 0 to disable, 1 to enable fan control policy support");
/* End of file */
