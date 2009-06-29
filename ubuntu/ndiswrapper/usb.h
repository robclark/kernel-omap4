/*
 *  Copyright (C) 2004 Jan Kiszka
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

#ifndef _USB_H_
#define _USB_H_

#include "ntoskernel.h"

#define IOCTL_INTERNAL_USB_SUBMIT_URB			0x00220003
#define IOCTL_INTERNAL_USB_RESET_PORT			0x00220007
#define IOCTL_INTERNAL_USB_GET_PORT_STATUS		0x00220013
#define IOCTL_INTERNAL_USB_CYCLE_PORT			0x0022001F
#define IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION	0x00220027

#define URB_FUNCTION_SELECT_CONFIGURATION            0x0000
#define URB_FUNCTION_SELECT_INTERFACE                0x0001
#define URB_FUNCTION_ABORT_PIPE                      0x0002
#define URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL       0x0003
#define URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL    0x0004
#define URB_FUNCTION_GET_FRAME_LENGTH                0x0005
#define URB_FUNCTION_SET_FRAME_LENGTH                0x0006
#define URB_FUNCTION_GET_CURRENT_FRAME_NUMBER        0x0007
#define URB_FUNCTION_CONTROL_TRANSFER                0x0008
#define URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER      0x0009
#define URB_FUNCTION_ISOCH_TRANSFER                  0x000A
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE      0x000B
#define URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE        0x000C
#define URB_FUNCTION_SET_FEATURE_TO_DEVICE           0x000D
#define URB_FUNCTION_SET_FEATURE_TO_INTERFACE        0x000E
#define URB_FUNCTION_SET_FEATURE_TO_ENDPOINT         0x000F
#define URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE         0x0010
#define URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE      0x0011
#define URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT       0x0012
#define URB_FUNCTION_GET_STATUS_FROM_DEVICE          0x0013
#define URB_FUNCTION_GET_STATUS_FROM_INTERFACE       0x0014
#define URB_FUNCTION_GET_STATUS_FROM_ENDPOINT        0x0015
#define URB_FUNCTION_RESERVED_0X0016                 0x0016
#define URB_FUNCTION_VENDOR_DEVICE                   0x0017
#define URB_FUNCTION_VENDOR_INTERFACE                0x0018
#define URB_FUNCTION_VENDOR_ENDPOINT                 0x0019
#define URB_FUNCTION_CLASS_DEVICE                    0x001A
#define URB_FUNCTION_CLASS_INTERFACE                 0x001B
#define URB_FUNCTION_CLASS_ENDPOINT                  0x001C
#define URB_FUNCTION_RESERVE_0X001D                  0x001D
#define URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL 0x001E
#define URB_FUNCTION_CLASS_OTHER                     0x001F
#define URB_FUNCTION_VENDOR_OTHER                    0x0020
#define URB_FUNCTION_GET_STATUS_FROM_OTHER           0x0021
#define URB_FUNCTION_CLEAR_FEATURE_TO_OTHER          0x0022
#define URB_FUNCTION_SET_FEATURE_TO_OTHER            0x0023
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT    0x0024
#define URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT      0x0025
#define URB_FUNCTION_GET_CONFIGURATION               0x0026
#define URB_FUNCTION_GET_INTERFACE                   0x0027
#define URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE   0x0028
#define URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE     0x0029
#define URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR       0x002A
#define URB_FUNCTION_RESERVE_0X002B                  0x002B
#define URB_FUNCTION_RESERVE_0X002C                  0x002C
#define URB_FUNCTION_RESERVE_0X002D                  0x002D
#define URB_FUNCTION_RESERVE_0X002E                  0x002E
#define URB_FUNCTION_RESERVE_0X002F                  0x002F
// USB 2.0 calls start at 0x0030
#define URB_FUNCTION_SYNC_RESET_PIPE                 0x0030
#define URB_FUNCTION_SYNC_CLEAR_STALL                0x0031
#define URB_FUNCTION_CONTROL_TRANSFER_EX             0x0032

#define USBD_PF_CHANGE_MAX_PACKET		0x00000001

#define USBD_TRANSFER_DIRECTION_OUT		0
#define USBD_TRANSFER_DIRECTION_IN		1

#define USBD_SHORT_TRANSFER_OK			0x00000002
#define USBD_START_ISO_TRANSFER_ASAP		0x00000004
#define USBD_DEFAULT_PIPE_TRANSFER		0x00000008

#define USBD_TRANSFER_DIRECTION(flags)		\
	((flags) & USBD_TRANSFER_DIRECTION_IN)

enum pipe_type {UsbdPipeTypeControl = USB_ENDPOINT_XFER_CONTROL,
		UsbdPipeTypeIsochronous = USB_ENDPOINT_XFER_ISOC,
		UsbdPipeTypeBulk = USB_ENDPOINT_XFER_BULK,
		UsbdPipeTypeInterrupt = USB_ENDPOINT_XFER_INT};

#define USBD_IS_BULK_PIPE(pipe_handle)					\
	(((pipe_handle)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)	\
	 == USB_ENDPOINT_XFER_BULK)

#define USBD_IS_INT_PIPE(pipe_handle)					\
	(((pipe_handle)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)	\
	 == USB_ENDPOINT_XFER_INT)

#define USBD_PORT_ENABLED			0x00000001
#define USBD_PORT_CONNECTED			0x00000002

typedef LONG USBD_STATUS;

#define USBD_STATUS_SUCCESS			0x0
#define USBD_STATUS_PENDING			0x40000000
#define USBD_STATUS_CANCELED			0x00010000

#define USBD_STATUS_CRC				0xC0000001
#define USBD_STATUS_BTSTUFF			0xC0000002
#define USBD_STATUS_DATA_TOGGLE_MISMATCH	0xC0000003
#define USBD_STATUS_STALL_PID			0xC0000004
#define USBD_STATUS_DEV_NOT_RESPONDING		0xC0000005
#define USBD_STATUS_PID_CHECK_FAILURE		0xC0000006
#define USBD_STATUS_UNEXPECTED_PID	     	0xC0000007
#define USBD_STATUS_DATA_OVERRUN		0xC0000008
#define USBD_STATUS_DATA_UNDERRUN		0xC0000009
#define USBD_STATUS_RESERVED1			0xC000000A
#define USBD_STATUS_RESERVED2			0xC000000B
#define USBD_STATUS_BUFFER_OVERRUN		0xC000000C
#define USBD_STATUS_BUFFER_UNDERRUN		0xC000000D
#define USBD_STATUS_NOT_ACCESSED		0xC000000F
#define USBD_STATUS_FIFO			0xC0000010
#define USBD_STATUS_XACT_ERROR			0xC0000011
#define USBD_STATUS_BABBLE_DETECTED		0xC0000012
#define USBD_STATUS_DATA_BUFFER_ERROR		0xC0000013

#define USBD_STATUS_NOT_SUPPORTED		0xC0000E00
#define USBD_STATUS_BUFFER_TOO_SMALL		0xC0003000
#define USBD_STATUS_TIMEOUT			0xC0006000
#define USBD_STATUS_DEVICE_GONE			0xC0007000

#define USBD_STATUS_NO_MEMORY			0x80000100
#define USBD_STATUS_INVALID_URB_FUNCTION	0x80000200
#define USBD_STATUS_INVALID_PARAMETER		0x80000300
#define USBD_STATUS_REQUEST_FAILED		0x80000500
#define USBD_STATUS_INVALID_PIPE_HANDLE		0x80000600
#define USBD_STATUS_ERROR_SHORT_TRANSFER	0x80000900

#define USBD_DEFAULT_MAXIMUM_TRANSFER_SIZE	PAGE_SIZE

struct urb_hcd_area {
	void *reserved8[8];
};

typedef struct usb_endpoint_descriptor *usbd_pipe_handle;
typedef struct usb_descriptor_header usb_common_descriptor_t;

struct usbd_pipe_information {
	USHORT wMaxPacketSize;
	UCHAR bEndpointAddress;
	UCHAR bInterval;
	enum pipe_type type;
	usbd_pipe_handle handle;
	ULONG max_tx_size;
	ULONG flags;
};

struct usbd_interface_information {
	USHORT bLength;
	UCHAR bInterfaceNumber;
	UCHAR bAlternateSetting;
	UCHAR bInterfaceClass;
	UCHAR bInterfaceSubClass;
	UCHAR bInterfaceProtocol;
	UCHAR reserved;
	void *handle;
	ULONG bNumEndpoints;
	struct usbd_pipe_information pipes[1];
};

struct usbd_interface_list_entry {
	struct usb_interface_descriptor *intf_desc;
	struct usbd_interface_information *intf;
};

struct nt_urb_header {
	USHORT length;
	USHORT function;
	USBD_STATUS status;
	void *usbd_dev_handle;
	ULONG usbd_flags;
};

struct usbd_select_interface {
	struct nt_urb_header header;
	void *handle;
	struct usbd_interface_information intf;
};

struct usbd_select_configuration {
	struct nt_urb_header header;
	struct usb_config_descriptor *config;
	void *handle;
	struct usbd_interface_information intf;
};

struct usbd_control_descriptor_request {
	struct nt_urb_header header;
	void *reserved;
	ULONG reserved0;
	ULONG transfer_buffer_length;
	void *transfer_buffer;
	struct mdl *mdl;
	union nt_urb *urb_link;
	struct urb_hcd_area hca;
	USHORT reserved1;
	UCHAR index;
	UCHAR desc_type;
	USHORT language_id;
	USHORT reserved2;
};

struct usbd_bulk_or_intr_transfer {
	struct nt_urb_header header;
	usbd_pipe_handle pipe_handle;
	ULONG transfer_flags;
	ULONG transfer_buffer_length;
	void *transfer_buffer;
	struct mdl *mdl;
	union nt_urb *urb_link;
	struct urb_hcd_area hca;
};

struct usbd_pipe_request {
	struct nt_urb_header header;
	usbd_pipe_handle pipe_handle;
};

struct usbd_vendor_or_class_request {
	struct nt_urb_header header;
	void *reserved;
	ULONG transfer_flags;
	ULONG transfer_buffer_length;
	void *transfer_buffer;
	struct mdl *mdl;
	union nt_urb *link;
	struct urb_hcd_area hca;
	UCHAR reserved_bits;
	UCHAR request;
	USHORT value;
	USHORT index;
	USHORT reserved1;
};

struct urb_control_feature_request {
	struct nt_urb_header header;
	void *reserved;
	ULONG reserved2;
	ULONG reserved3;
	void *reserved4;
	struct mdl *reserved5;
	union nt_urb *link;
	struct urb_hcd_area hca;
	USHORT reserved0;
	USHORT feature_selector;
	USHORT index;
	USHORT reserved1;
};

struct urb_control_get_status_request {
	struct nt_urb_header header;
	void *reserved;
	ULONG reserved0;
	ULONG transfer_buffer_length;
	void *transfer_buffer;
	struct mdl *mdl;
	union nt_urb *link;
	struct urb_hcd_area hca;
	UCHAR reserved1[4];
	USHORT index;
	USHORT reserved2;
};

struct usbd_iso_packet_desc {
	ULONG offset;
	ULONG length;
	USBD_STATUS status;
};

struct usbd_isochronous_transfer {
	struct nt_urb_header header;
	usbd_pipe_handle pipe_handle;
	ULONG transfer_flags;
	ULONG transfer_buffer_length;
	void *transfer_buffer;
	struct mdl *mdl;
	union nt_urb *urb_link;
	struct urb_hcd_area hca;
	ULONG start_frame;
	ULONG number_of_packets;
	ULONG error_count;
	struct usbd_iso_packet_desc iso_packet[1];
};

union nt_urb {
	struct nt_urb_header header;
	struct usbd_select_interface select_intf;
	struct usbd_select_configuration select_conf;
	struct usbd_bulk_or_intr_transfer bulk_int_transfer;
	struct usbd_control_descriptor_request control_desc;
	struct usbd_vendor_or_class_request vendor_class_request;
	struct usbd_isochronous_transfer isochronous;
	struct usbd_pipe_request pipe_req;
	struct urb_control_feature_request feat_req;
	struct urb_control_get_status_request status_req;
};

struct usbd_bus_interface_usbdi {
	USHORT Size;
	USHORT Version;
	void *Context;
	void *InterfaceReference;
	void *InterfaceDereference;
	void *GetUSBDIVersion;
	void *QueryBusTime;
	void *SubmitIsoOutUrb;
	void *QueryBusInformation;
	/* version 1 and above have following field */
	void *IsDeviceHighSpeed;
	/* version 2 (and above) have following field */
	void *LogEntry;
};

struct usbd_bus_information_level {
	ULONG TotalBandwidth;
	ULONG ConsumedBandwidth;
	/* level 1 and above have following fields */
	ULONG ControllerNameLength;
	wchar_t ControllerName[1];
};

#define USBDI_VERSION_XP			0x00000500 // Windows XP
#define USB_HCD_CAPS_SUPPORTS_RT_THREADS	0x00000001
#define USB_BUSIF_USBDI_VERSION_0		0x0000
#define USB_BUSIF_USBDI_VERSION_1		0x0001
#define USB_BUSIF_USBDI_VERSION_2		0x0002

struct usbd_version_info {
	ULONG usbdi_version;
	ULONG supported_usb_version;
};

struct usbd_idle_callback {
	void *callback;
	void *context;
};

#define NT_URB_STATUS(nt_urb) ((nt_urb)->header.status)

NTSTATUS wrap_submit_irp(struct device_object *pdo, struct irp *irp);
void wrap_suspend_urbs(struct wrap_device *wd);
void wrap_resume_urbs(struct wrap_device *wd);

void USBD_InterfaceGetUSBDIVersion(void *context,
				   struct usbd_version_info *version_info,
				   ULONG *hcd_capa) wstdcall;
BOOLEAN USBD_InterfaceIsDeviceHighSpeed(void *context) wstdcall;
void USBD_InterfaceReference(void *context) wstdcall;
void USBD_InterfaceDereference(void *context) wstdcall;
NTSTATUS USBD_InterfaceQueryBusTime(void *context, ULONG *frame) wstdcall;
NTSTATUS USBD_InterfaceSubmitIsoOutUrb(void *context,
				       union nt_urb *nt_urb) wstdcall;
NTSTATUS USBD_InterfaceQueryBusInformation(void *context, ULONG level, void *buf,
					   ULONG *buf_length,
					   ULONG *buf_actual_length) wstdcall;
NTSTATUS USBD_InterfaceLogEntry(void *context, ULONG driver_tag, ULONG enum_tag,
				ULONG p1, ULONG p2) wstdcall;

#endif /* USB_H */
