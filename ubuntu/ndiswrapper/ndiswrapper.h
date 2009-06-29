/*
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
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

#ifndef _NDISWRAPPER_H_
#define _NDISWRAPPER_H_

#define DRIVER_VERSION "1.55"
#define UTILS_VERSION "1.9"

#define DRIVER_NAME "ndiswrapper"
#define DRIVER_CONFIG_DIR "/etc/ndiswrapper"

#define SSID_MAX_WPA_IE_LEN 40
#define NDIS_ESSID_MAX_SIZE 32
#define NDIS_ENCODING_TOKEN_MAX 32
#define MAX_ENCR_KEYS 4
#define TX_RING_SIZE 16
#define NDIS_MAX_RATES 8
#define NDIS_MAX_RATES_EX 16
#define WLAN_EID_GENERIC 221
#define MAX_WPA_IE_LEN 64
#define MAX_STR_LEN 512

#define WRAP_PCI_BUS 5
#define WRAP_PCMCIA_BUS 8
/* some USB devices, e.g., DWL-G120 have BusType as 0 */
#define WRAP_INTERNAL_BUS 0
/* documentation at msdn says 15 is PNP bus, but inf files from all
 * vendors say 15 is USB; which is correct? */
#define WRAP_USB_BUS 15

/* NDIS device must be 0, for compatability with old versions of
 * ndiswrapper where device type for NDIS drivers is 0 */
#define WRAP_NDIS_DEVICE 0
#define WRAP_USB_DEVICE 1
#define WRAP_BLUETOOTH_DEVICE1 2
#define WRAP_BLUETOOTH_DEVICE2 3

#define WRAP_DEVICE_BUS(dev, bus) ((dev) << 8 | (bus))
#define WRAP_BUS(dev_bus) ((dev_bus) & 0x000FF)
#define WRAP_DEVICE(dev_bus) ((dev_bus) >> 8)

#define MAX_DRIVER_NAME_LEN 32
#define MAX_VERSION_STRING_LEN 64
#define MAX_SETTING_NAME_LEN 128
#define MAX_SETTING_VALUE_LEN 256

#define MAX_DRIVER_PE_IMAGES 4
#define MAX_DRIVER_BIN_FILES 5
#define MAX_DEVICE_SETTINGS 512

#define MAX_ALLOCATED_URBS 15

#define DEV_ANY_ID -1

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTRSEP "%02x:%02x:%02x:%02x:%02x:%02x"
#define MACSTR "%02x%02x%02x%02x%02x%02x"
#define MACINTADR(a) (int*)&((a)[0]), (int*)&((a)[1]), (int*)&((a)[2]), \
		(int*)&((a)[3]), (int*)&((a)[4]), (int*)&((a)[5])

#ifdef __KERNEL__
/* DEBUG macros */

#define MSG(level, fmt, ...)				\
	printk(level "ndiswrapper (%s:%d): " fmt "\n",	\
	       __func__, __LINE__ , ## __VA_ARGS__)

#define WARNING(fmt, ...) MSG(KERN_WARNING, fmt, ## __VA_ARGS__)
#define ERROR(fmt, ...) MSG(KERN_ERR, fmt , ## __VA_ARGS__)
#define INFO(fmt, ...) MSG(KERN_INFO, fmt , ## __VA_ARGS__)
#define TODO() WARNING("not fully implemented (yet)")

#define TRACE(fmt, ...) do { } while (0)
#define TRACE1(fmt, ...) do { } while (0)
#define TRACE2(fmt, ...) do { } while (0)
#define TRACE3(fmt, ...) do { }  while (0)
#define TRACE4(fmt, ...) do { } while (0)
#define TRACE5(fmt, ...) do { } while (0)
#define TRACE6(fmt, ...) do { } while (0)

/* for a block of code */
#define DBG_BLOCK(level) while (0)

extern int debug;

#if defined DEBUG
#undef TRACE
#define TRACE(level, fmt, ...)				       \
do {								       \
	if (debug >= level)					       \
		printk(KERN_INFO "%s (%s:%d): " fmt "\n", DRIVER_NAME, \
		       __func__, __LINE__ , ## __VA_ARGS__);       \
} while (0)
#undef DBG_BLOCK
#define DBG_BLOCK(level) if (debug >= level)
#endif

#if defined(DEBUG) && DEBUG >= 1
#undef TRACE1
#define TRACE1(fmt, ...) TRACE(1, fmt , ## __VA_ARGS__)
#endif

#if defined(DEBUG) && DEBUG >= 2
#undef TRACE2
#define TRACE2(fmt, ...) TRACE(2, fmt , ## __VA_ARGS__)
#endif

#if defined(DEBUG) && DEBUG >= 3
#undef TRACE3
#define TRACE3(fmt, ...) TRACE(3, fmt , ## __VA_ARGS__)
#endif

#if defined(DEBUG) && DEBUG >= 4
#undef TRACE4
#define TRACE4(fmt, ...) TRACE(4, fmt , ## __VA_ARGS__)
#endif

#if defined(DEBUG) && DEBUG >= 5
#undef TRACE5
#define TRACE5(fmt, ...) TRACE(5, fmt , ## __VA_ARGS__)
#endif

#if defined(DEBUG) && DEBUG >= 6
#undef TRACE6
#define TRACE6(fmt, ...) TRACE(6, fmt , ## __VA_ARGS__)
#endif

#define ENTER1(fmt, ...) TRACE1("Enter " fmt , ## __VA_ARGS__)
#define ENTER2(fmt, ...) TRACE2("Enter " fmt , ## __VA_ARGS__)
#define ENTER3(fmt, ...) TRACE3("Enter " fmt , ## __VA_ARGS__)
#define ENTER4(fmt, ...) TRACE4("Enter " fmt , ## __VA_ARGS__)
#define ENTER5(fmt, ...) TRACE5("Enter " fmt , ## __VA_ARGS__)
#define ENTER6(fmt, ...) TRACE6("Enter " fmt , ## __VA_ARGS__)

#define EXIT1(stmt) do { TRACE1("Exit"); stmt; } while(0)
#define EXIT2(stmt) do { TRACE2("Exit"); stmt; } while(0)
#define EXIT3(stmt) do { TRACE3("Exit"); stmt; } while(0)
#define EXIT4(stmt) do { TRACE4("Exit"); stmt; } while(0)
#define EXIT5(stmt) do { TRACE5("Exit"); stmt; } while(0)
#define EXIT6(stmt) do { TRACE6("Exit"); stmt; } while(0)

#if defined(USB_DEBUG)
#define USBTRACE TRACE1
#define USBENTER ENTER1
#define USBEXIT EXIT1
#else
#define USBTRACE(fmt, ...)
#define USBENTER(fmt, ...)
#define USBEXIT(stmt) stmt
#endif

#if defined(EVENT_DEBUG)
#define EVENTTRACE TRACE1
#define EVENTENTER ENTER1
#define EVENTEXIT EXIT1
#else
#define EVENTTRACE(fmt, ...)
#define EVENTENTER(fmt, ...)
#define EVENTEXIT(stmt) stmt
#endif

#if defined(TIMER_DEBUG)
#define TIMERTRACE TRACE1
#define TIMERENTER ENTER1
#define TIMEREXIT EXIT1
#else
#define TIMERTRACE(fmt, ...)
#define TIMERENTER(fmt, ...)
#define TIMEREXIT(stmt) stmt
#endif

#if defined(IO_DEBUG)
#define IOTRACE TRACE1
#define IOENTER ENTER1
#define IOEXIT EXIT1
#else
#define IOTRACE(fmt, ...)
#define IOENTER(fmt, ...)
#define IOEXIT(stmt) stmt
#endif

#if defined(WORK_DEBUG)
#define WORKTRACE TRACE1
#define WORKENTER ENTER1
#define WORKEXIT EXIT1
#else
#define WORKTRACE(fmt, ...)
#define WORKENTER(fmt, ...)
#define WORKEXIT(stmt) stmt
#endif

#ifdef DEBUG
#define assert(expr)							\
do {									\
	if (!(expr)) {							\
		ERROR("assertion '%s' failed", #expr);			\
		dump_stack();						\
	}								\
} while (0)
#else
#define assert(expr) do { } while (0)
#endif

#endif // __KERNEL__

#endif // NDISWRAPPER_H
