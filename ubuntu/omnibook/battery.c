/*
 * battery.c -- battery related functions
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

struct omnibook_battery_info {
	u8 type;		/* 1 - Li-Ion, 2 NiMH */
	u16 sn;			/* Serial number */
	u16 dv;			/* Design Voltage */
	u16 dc;			/* Design Capacity */
};

struct omnibook_battery_state {
	u16 pv;			/* Present Voltage */
	u16 rc;			/* Remaining Capacity */
	u16 lc;			/* Last Full Capacity */
	u8 gauge;		/* Gauge in % */
	u8 status;		/* 0 - unknown, 1 - charged, 2 - discharging, 3 - charging, 4 - critical) */
};

enum {
	OMNIBOOK_BATTSTAT_UNKNOWN,
	OMNIBOOK_BATTSTAT_CHARGED,
	OMNIBOOK_BATTSTAT_DISCHARGING,
	OMNIBOOK_BATTSTAT_CHARGING,
	OMNIBOOK_BATTSTAT_CRITICAL
};

#define BAT_OFFSET 0x10

static int __backend_u16_read(struct omnibook_operation *io_op, u16 *data)
{
	int retval;
	u8 byte;

	retval = __backend_byte_read(io_op, &byte);
	if (retval)
		return retval;
	*data = byte;
	io_op->read_addr += 1;
	retval = __backend_byte_read(io_op, &byte);
	*data += (byte << 8);
	return retval;
}

static int omnibook_battery_present(struct omnibook_operation *io_op, int num)
{
	int retval;
	u8 bat;
	int i;

	/*
	 * XE3GF
	 * TSP10
	 * TSM30X
	 * TSM70
	 */
	if (omnibook_ectype & (XE3GF | TSP10 | TSM70 | TSM30X)) {
		io_op->read_addr = XE3GF_BAL;
		io_op->read_mask = XE3GF_BAL0_MASK;
		for (i = 0; i < num; i++)
			io_op->read_mask = io_op->read_mask << 1;
		retval = __backend_byte_read(io_op, &bat);
	/*
	 * XE3GC
	 * AMILOD
	 */
	} else if (omnibook_ectype & (XE3GC | AMILOD)) {
		io_op->read_addr = XE3GC_BAT;
		io_op->read_mask = XE3GC_BAT0_MASK;
		for (i = 0; i < num; i++)
			io_op->read_mask = io_op->read_mask << 1;
		retval = __backend_byte_read(io_op, &bat);
	} else
		retval = -ENODEV;

	/* restore default read_mask */
	io_op->read_mask = 0;

	return !!bat;
}

/*
 * Get static battery information
 * All info have to be reread every time because battery sould be cahnged
 * when laptop is on AC power 
 * return values:
 *  < 0 - ERROR
 *    0 - OK
 *    1 - Battery is not present
 *    2 - Not supported
 */
static int omnibook_get_battery_info(struct omnibook_operation *io_op,
				     int num,
				     struct omnibook_battery_info *battinfo)
{
	int retval;
	/*
	 * XE3GF
	 * TSP10
	 * TSM70
         * TSM30X
	 */
	if (omnibook_ectype & (XE3GF | TSP10 | TSM70 | TSM30X)) {
		retval = omnibook_battery_present(io_op, num);
		if (retval < 0)
			return retval;
		if (retval) {
			io_op->read_addr = XE3GF_BTY0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &(*battinfo).type)))
				return retval;
			io_op->read_addr = XE3GF_BSN0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).sn)))
				return retval;
			io_op->read_addr = XE3GF_BDV0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).dv)))
				return retval;
			io_op->read_addr = XE3GF_BDC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).dc)))
				return retval;

			(*battinfo).type = ((*battinfo).type & XE3GF_BTY_MASK) ? 1 : 0;
		} else
			return 1;
	/*
	 * XE3GC
	 */
	} else if (omnibook_ectype & (XE3GC)) {
		retval = omnibook_battery_present(io_op, num);
		if (retval < 0)
			return retval;
		if (retval) {
			io_op->read_addr = XE3GC_BDV0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).dv)))
				return retval;
			io_op->read_addr = XE3GC_BDC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).dc)))
				return retval;
			io_op->read_addr = XE3GC_BTY0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &(*battinfo).type)))
				return retval;

			(*battinfo).type = ((*battinfo).type & XE3GC_BTY_MASK) ? 1 : 0;
			(*battinfo).sn = 0;	/* Unknown */
		} else
			return 1;
		/*
		 * AMILOD
		 */
	} else if (omnibook_ectype & (AMILOD)) {
		retval = omnibook_battery_present(io_op, num);
		if (retval < 0)
			return retval;
		if (retval) {
			io_op->read_addr = AMILOD_BDV0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).dv)))
				return retval;
			io_op->read_addr = AMILOD_BDC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battinfo).dc)))
				return retval;
			io_op->read_addr = AMILOD_BTY0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &(*battinfo).type)))
				return retval;

			(*battinfo).type = ((*battinfo).type & AMILOD_BTY_MASK) ? 1 : 0;
			(*battinfo).sn = 0;	/* Unknown */
		} else
			return 1;
		/*
		 * FIXME
		 * OB500
		 * OB510
		 */
	} else if (omnibook_ectype & (OB500 | OB510)) {
		switch (num) {
		case 0:
		case 1:
		case 2:
			break;
		default:
			return -EINVAL;
		}
		/*
		 * OB6000
		 * OB6100
		 * XE4500
		 */
	} else if (omnibook_ectype & (OB6000 | OB6100 | XE4500)) {
		switch (num) {
		case 0:
		case 1:
			break;
		default:
			return -EINVAL;
		}
	} else
		return 2;

	return 0;
}

/*
 * Get battery status
 * return values:
 *  < 0 - ERROR
 *    0 - OK
 *    1 - Battery is not present
 *    2 - Not supported
 */
static int omnibook_get_battery_status(struct omnibook_operation *io_op, 
				       int num,
				       struct omnibook_battery_state *battstat)
{
	int retval;
	u8 status;
	u16 dc;
	int gauge;

	/*
	 * XE3GF
	 * TSP10
	 * TSM70
	 */
	if (omnibook_ectype & (XE3GF | TSP10 | TSM70 | TSM30X)) {
		retval = omnibook_battery_present(io_op, num);
		if (retval < 0)
			return retval;
		if (retval) {
			io_op->read_addr = XE3GF_BST0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = XE3GF_BRC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = XE3GF_BPV0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			io_op->read_addr = XE3GF_BFC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).lc)))
				return retval;
			io_op->read_addr = XE3GF_GAU0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &(*battstat).gauge)))
				return retval;

			if (status & XE3GF_BST_MASK_CRT)
				(*battstat).status = OMNIBOOK_BATTSTAT_CRITICAL;
			else if (status & XE3GF_BST_MASK_CHR)
				(*battstat).status = OMNIBOOK_BATTSTAT_CHARGING;
			else if (status & XE3GF_BST_MASK_DSC)
				(*battstat).status = OMNIBOOK_BATTSTAT_DISCHARGING;
			else if (status & (XE3GF_BST_MASK_CHR | XE3GF_BST_MASK_DSC))
				(*battstat).status = OMNIBOOK_BATTSTAT_UNKNOWN;
			else {
				(*battstat).status = OMNIBOOK_BATTSTAT_CHARGED;
			}
		} else
			return 1;
	/*
	 * XE3GC
	 */
	} else if (omnibook_ectype & (XE3GC)) {
		retval = omnibook_battery_present(io_op, num);
		if (retval < 0)
			return retval;
		if (retval) {
			io_op->read_addr = XE3GC_BST0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = XE3GC_BRC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = XE3GC_BPV0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			io_op->read_addr = XE3GC_BDC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &dc)))
				return retval;

			if (status & XE3GC_BST_MASK_CRT)
				(*battstat).status = OMNIBOOK_BATTSTAT_CRITICAL;
			else if (status & XE3GC_BST_MASK_CHR)
				(*battstat).status = OMNIBOOK_BATTSTAT_CHARGING;
			else if (status & XE3GC_BST_MASK_DSC)
				(*battstat).status = OMNIBOOK_BATTSTAT_DISCHARGING;
			else if (status & (XE3GC_BST_MASK_CHR | XE3GC_BST_MASK_DSC))
				(*battstat).status = OMNIBOOK_BATTSTAT_UNKNOWN;
			else {
				(*battstat).status = OMNIBOOK_BATTSTAT_CHARGED;
			}
			gauge = ((*battstat).rc * 100) / dc;
			(*battstat).gauge = gauge;
			(*battstat).lc = 0;	/* Unknown */
		} else
			return 1;
	/*
	 * AMILOD
	 */
	} else if (omnibook_ectype & (AMILOD)) {
		retval = omnibook_battery_present(io_op, num);
		if (retval < 0)
			return retval;
		if (retval) {
			io_op->read_addr = AMILOD_BST0 + (BAT_OFFSET * num);
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = AMILOD_BRC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = AMILOD_BPV0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			io_op->read_addr = AMILOD_BDC0 + (BAT_OFFSET * num);
			if ((retval = __backend_u16_read(io_op, &dc)))
				return retval;

			if (status & AMILOD_BST_MASK_CRT)
				(*battstat).status = OMNIBOOK_BATTSTAT_CRITICAL;
			else if (status & AMILOD_BST_MASK_CHR)
				(*battstat).status = OMNIBOOK_BATTSTAT_CHARGING;
			else if (status & AMILOD_BST_MASK_DSC)
				(*battstat).status = OMNIBOOK_BATTSTAT_DISCHARGING;
			else if (status & (AMILOD_BST_MASK_CHR | AMILOD_BST_MASK_DSC))
				(*battstat).status = OMNIBOOK_BATTSTAT_UNKNOWN;
			else {
				(*battstat).status = OMNIBOOK_BATTSTAT_CHARGED;
			}
			gauge = ((*battstat).rc * 100) / dc;
			(*battstat).gauge = gauge;
			(*battstat).lc = 0;	/* Unknown */
		} else
			return 1;
		/*
		 * OB500
		 * OB510
		 */
	} else if (omnibook_ectype & (OB500 | OB510)) {
		switch (num) {
		case 0:
			io_op->read_addr = OB500_BT1S;
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = OB500_BT1C;
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = OB500_BT1V;
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			break;
		case 1:
			io_op->read_addr = OB500_BT2S;
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = OB500_BT2C;
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = OB500_BT2V;
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			break;
		case 2:
			io_op->read_addr = OB500_BT3S;
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = OB500_BT3C;
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = OB500_BT3V;
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			break;
		default:
			return -EINVAL;
		}
		if (status & OB500_BST_MASK_CRT)
			(*battstat).status = OMNIBOOK_BATTSTAT_CRITICAL;
		else if (status & OB500_BST_MASK_CHR)
			(*battstat).status = OMNIBOOK_BATTSTAT_CHARGING;
		else if (status & OB500_BST_MASK_DSC)
			(*battstat).status = OMNIBOOK_BATTSTAT_DISCHARGING;
		else if (status & (OB500_BST_MASK_CHR | OB500_BST_MASK_DSC))
			(*battstat).status = OMNIBOOK_BATTSTAT_UNKNOWN;
		else {
			(*battstat).status = OMNIBOOK_BATTSTAT_CHARGED;
		}
		/*
		 * OB6000
		 * OB6100
		 * XE4500
		 */
	} else if (omnibook_ectype & (OB6000 | OB6100 | XE4500)) {
		switch (num) {
		case 0:
			io_op->read_addr = OB500_BT1S;
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = OB500_BT1C;
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = OB500_BT1V;
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			break;
		case 1:
			io_op->read_addr = OB500_BT3S;
			if ((retval = __backend_byte_read(io_op, &status)))
				return retval;
			io_op->read_addr = OB500_BT3C;
			if ((retval = __backend_u16_read(io_op, &(*battstat).rc)))
				return retval;
			io_op->read_addr = OB500_BT3V;
			if ((retval = __backend_u16_read(io_op, &(*battstat).pv)))
				return retval;
			break;
		default:
			return -EINVAL;
		}
		if (status & OB500_BST_MASK_CRT)
			(*battstat).status = OMNIBOOK_BATTSTAT_CRITICAL;
		else if (status & OB500_BST_MASK_CHR)
			(*battstat).status = OMNIBOOK_BATTSTAT_CHARGING;
		else if (status & OB500_BST_MASK_DSC)
			(*battstat).status = OMNIBOOK_BATTSTAT_DISCHARGING;
		else if (status & (OB500_BST_MASK_CHR | OB500_BST_MASK_DSC))
			(*battstat).status = OMNIBOOK_BATTSTAT_UNKNOWN;
		else {
			(*battstat).status = OMNIBOOK_BATTSTAT_CHARGED;
		}
	} else {
		return 2;
	}
	return 0;
}

static int omnibook_battery_read(char *buffer, struct omnibook_operation *io_op)
{
	char *statustr;
	char *typestr;
	int max = 0;
	int num = 0;
	int len = 0;
	int retval;
	int i;
	struct omnibook_battery_info battinfo;
	struct omnibook_battery_state battstat;
	/*
	 * XE3GF
	 * XE3GC
	 * 0B6000
	 * 0B6100
	 * XE4500
	 * AMILOD
	 * TSP10
	 */
	if (omnibook_ectype & (XE3GF | XE3GC | OB6000 | OB6100 | XE4500 | AMILOD | TSP10))
		max = 2;
	/*
	 * OB500
	 * 0B510
	 */
	else if (omnibook_ectype & (OB500 | OB510))
		max = 3;
	/*
	 * TSM30X
	 * TSM70
	 */
	else if (omnibook_ectype & (TSM70 | TSM30X))
		max = 1;

	if(mutex_lock_interruptible(&io_op->backend->mutex))
			return -ERESTARTSYS;

	for (i = 0; i < max; i++) {
		retval = omnibook_get_battery_info(io_op, i, &battinfo);
		if (retval == 0) {
			num++;
			omnibook_get_battery_status(io_op, i, &battstat);
			typestr = (battinfo.type) ? "Li-Ion" : "NiMH";
			switch (battstat.status) {
			case OMNIBOOK_BATTSTAT_CHARGED:
				statustr = "charged";
				break;
			case OMNIBOOK_BATTSTAT_DISCHARGING:
				statustr = "discharging";
				break;
			case OMNIBOOK_BATTSTAT_CHARGING:
				statustr = "charging";
				break;
			case OMNIBOOK_BATTSTAT_CRITICAL:
				statustr = "critical";
				break;
			default:
				statustr = "unknown";
			}

			len += sprintf(buffer + len, "Battery:            %11d\n", i);
			len += sprintf(buffer + len, "Type:               %11s\n", typestr);
			if (battinfo.sn)
				len +=
				    sprintf(buffer + len, "Serial Number:      %11d\n",
					    battinfo.sn);
			len += sprintf(buffer + len, "Present Voltage:    %11d mV\n", battstat.pv);
			len += sprintf(buffer + len, "Design Voltage:     %11d mV\n", battinfo.dv);
			len += sprintf(buffer + len, "Remaining Capacity: %11d mAh\n", battstat.rc);
			if (battstat.lc)
				len +=
				    sprintf(buffer + len, "Last Full Capacity: %11d mAh\n",
					    battstat.lc);
			len += sprintf(buffer + len, "Design Capacity:    %11d mAh\n", battinfo.dc);
			len +=
			    sprintf(buffer + len, "Gauge:              %11d %%\n", battstat.gauge);
			len += sprintf(buffer + len, "Status:             %11s\n", statustr);
			len += sprintf(buffer + len, "\n");
		}
	}
	if (num == 0)
		len += sprintf(buffer + len, "No battery present\n");

	mutex_unlock(&io_op->backend->mutex);

	return len;
}

static struct omnibook_tbl battery_table[] __initdata = {
	{XE3GF | XE3GC | AMILOD | TSP10 | TSM70 | TSM30X, {EC,}},
	{0,}
};

static struct omnibook_feature __declared_feature battery_driver = {
	.name = "battery",
#ifdef CONFIG_OMNIBOOK_LEGACY
	.enabled = 1,
#else
	.enabled = 0,
#endif
	.read = omnibook_battery_read,
	.ectypes = XE3GF | XE3GC | AMILOD | TSP10 | TSM70 | TSM30X,	/* FIXME: OB500|OB6000|OB6100|XE4500 */
	.tbl = battery_table,
};

module_param_named(battery, battery_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(battery, "Use 0 to disable, 1 to enable battery status monitoring");
/* End of file */
