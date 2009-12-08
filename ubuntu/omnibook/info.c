/*
 * info.c -- trivial informational features
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

#include <linux/dmi.h>
#include <linux/version.h>

static int omnibook_version_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;

	len += sprintf(buffer + len, "%s\n", OMNIBOOK_MODULE_VERSION);

	return len;
}

static int omnibook_dmi_read(char *buffer, struct omnibook_operation *io_op)
{
	int len = 0;

	len += sprintf(buffer + len, "BIOS Vendor:   %s\n", dmi_get_system_info(DMI_BIOS_VENDOR));
	len += sprintf(buffer + len, "BIOS Version:  %s\n", dmi_get_system_info(DMI_BIOS_VERSION));
	len += sprintf(buffer + len, "BIOS Release:  %s\n", dmi_get_system_info(DMI_BIOS_DATE));
	len += sprintf(buffer + len, "System Vendor: %s\n", dmi_get_system_info(DMI_SYS_VENDOR));
	len += sprintf(buffer + len, "Product Name:  %s\n", dmi_get_system_info(DMI_PRODUCT_NAME));
	len +=
	    sprintf(buffer + len, "Version:       %s\n", dmi_get_system_info(DMI_PRODUCT_VERSION));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
	len +=
	    sprintf(buffer + len, "Serial Number: %s\n", dmi_get_system_info(DMI_PRODUCT_SERIAL));
#endif
	len += sprintf(buffer + len, "Board Vendor:  %s\n", dmi_get_system_info(DMI_BOARD_VENDOR));
	len += sprintf(buffer + len, "Board Name:    %s\n", dmi_get_system_info(DMI_BOARD_VERSION));

	return len;
}

static struct omnibook_feature __declared_feature version_driver = {
	.name = "version",
	.enabled = 1,
	.read = omnibook_version_read,
};

static struct omnibook_feature __declared_feature dmi_driver = {
	.name = "dmi",
	.enabled = 1,
	.read = omnibook_dmi_read,
};

module_param_named(dmi, dmi_driver.enabled, int, S_IRUGO);
MODULE_PARM_DESC(dmi, "Use 0 to disable, 1 to enable DMI informations display support");

/* End of file */
