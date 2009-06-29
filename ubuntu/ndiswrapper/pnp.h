/*
 *  Copyright (C) 2005 Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#ifndef _PNP_H_
#define _PNP_H_

#include "ntoskernel.h"
#include "ndis.h"
#include "wrapndis.h"

NTSTATUS pnp_start_device(struct wrap_device *wd);
NTSTATUS pnp_stop_device(struct wrap_device *wd);
NTSTATUS pnp_remove_device(struct wrap_device *wd);

int wrap_pnp_start_pci_device(struct pci_dev *pdev,
			      const struct pci_device_id *ent);
void __devexit wrap_pnp_remove_pci_device(struct pci_dev *pdev);
int wrap_pnp_suspend_pci_device(struct pci_dev *pdev, pm_message_t state);
int wrap_pnp_resume_pci_device(struct pci_dev *pdev);

#ifdef ENABLE_USB
int wrap_pnp_start_usb_device(struct usb_interface *intf,
			      const struct usb_device_id *usb_id);
void wrap_pnp_remove_usb_device(struct usb_interface *intf);
int wrap_pnp_suspend_usb_device(struct usb_interface *intf,
				pm_message_t state);
int wrap_pnp_resume_usb_device(struct usb_interface *intf);
#endif

#endif
