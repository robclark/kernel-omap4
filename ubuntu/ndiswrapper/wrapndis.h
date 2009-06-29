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

#ifndef _WRAPNDIS_H_
#define _WRAPNDIS_H_

#include "ndis.h"
#include "pnp.h"

int wrapndis_init(void);
void wrapndis_exit(void);

NDIS_STATUS mp_reset(struct ndis_device *wnd);

NDIS_STATUS mp_request(enum ndis_request_type request,
		       struct ndis_device *wnd, ndis_oid oid,
		       void *buf, ULONG buflen, ULONG *written, ULONG *needed);

static inline NDIS_STATUS mp_query_info(struct ndis_device *wnd,
					ndis_oid oid, void *buf, ULONG buflen,
					ULONG *written, ULONG *needed)
{
	return mp_request(NdisRequestQueryInformation, wnd, oid,
			  buf, buflen, written, needed);
}

static inline NDIS_STATUS mp_set_info(struct ndis_device *wnd,
				      ndis_oid oid, void *buf, ULONG buflen,
				      ULONG *written, ULONG *needed)
{
	return mp_request(NdisRequestSetInformation, wnd, oid,
			  buf, buflen, written, needed);
}

static inline NDIS_STATUS mp_query(struct ndis_device *wnd, ndis_oid oid,
				   void *buf, ULONG buflen)
{
	return mp_request(NdisRequestQueryInformation, wnd, oid,
			  buf, buflen, NULL, NULL);
}

static inline NDIS_STATUS mp_query_int(struct ndis_device *wnd,
				       ndis_oid oid, ULONG *data)
{
	return mp_request(NdisRequestQueryInformation, wnd, oid,
			  data, sizeof(ULONG), NULL, NULL);
}

static inline NDIS_STATUS mp_set(struct ndis_device *wnd, ndis_oid oid,
				 void *buf, ULONG buflen)
{
	return mp_request(NdisRequestSetInformation, wnd, oid,
			  buf, buflen, NULL, NULL);
}

static inline NDIS_STATUS mp_set_int(struct ndis_device *wnd,
				     ndis_oid oid, ULONG data)
{
	return mp_request(NdisRequestSetInformation, wnd, oid,
			  &data, sizeof(ULONG), NULL, NULL);
}

void free_tx_packet(struct ndis_device *wnd, struct ndis_packet *packet,
		    NDIS_STATUS status);
int init_ndis_driver(struct driver_object *drv_obj);
NDIS_STATUS ndis_reinit(struct ndis_device *wnd);
void set_media_state(struct ndis_device *wnd, enum ndis_media_state state);

void hangcheck_add(struct ndis_device *wnd);
void hangcheck_del(struct ndis_device *wnd);

driver_dispatch_t winNdisDispatchPnp;
driver_dispatch_t winNdisDispatchPower;
driver_dispatch_t winNdisDispatchDeviceControl;

struct iw_statistics *get_iw_stats(struct net_device *dev);

#endif
