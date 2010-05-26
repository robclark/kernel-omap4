/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 *
 * heavily based on code from kernel/iscsi.c:
 *   Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>,
 *   licensed under the terms of the GNU GPL v2.0,
 */

#include <linux/ctype.h>
#include <scsi/scsi.h>

#include "iscsi.h"
#include "iscsi_dbg.h"

static int insert_disconnect_pg(u8 *ptr)
{
	unsigned char disconnect_pg[] = {0x02, 0x0e, 0x80, 0x80, 0x00, 0x0a, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	memcpy(ptr, disconnect_pg, sizeof(disconnect_pg));
	return sizeof(disconnect_pg);
}

static int insert_caching_pg(u8 *ptr, int wcache, int rcache)
{
	unsigned char caching_pg[] = {0x08, 0x12, 0x10, 0x00, 0xff, 0xff, 0x00, 0x00,
				      0xff, 0xff, 0xff, 0xff, 0x80, 0x14, 0x00, 0x00,
				      0x00, 0x00, 0x00, 0x00};

	memcpy(ptr, caching_pg, sizeof(caching_pg));
	if (wcache)
		ptr[2] |= 0x04;	/* set WCE bit if we're caching writes */
	if (!rcache)
		ptr[2] |= 0x01; /* Read Cache Disable */

	return sizeof(caching_pg);
}

static int insert_ctrl_m_pg(u8 *ptr)
{
	unsigned char ctrl_m_pg[] = {0x0a, 0x0a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x02, 0x4b};

	memcpy(ptr, ctrl_m_pg, sizeof(ctrl_m_pg));
	return sizeof(ctrl_m_pg);
}

static int insert_iec_m_pg(u8 *ptr)
{
	unsigned char iec_m_pg[] = {0x1c, 0xa, 0x08, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x00};

	memcpy(ptr, iec_m_pg, sizeof(iec_m_pg));
	return sizeof(iec_m_pg);
}

static int insert_format_m_pg(u8 *ptr, u32 sector_size)
{
	unsigned char format_m_pg[] = {0x03, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				       0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00,
				       0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00};

	memcpy(ptr, format_m_pg, sizeof(format_m_pg));
	ptr[12] = (sector_size >> 8) & 0xff;
	ptr[13] = sector_size & 0xff;
	return sizeof(format_m_pg);
}

static int insert_geo_m_pg(u8 *ptr, u64 sec)
{
	unsigned char geo_m_pg[] = {0x04, 0x16, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x3a, 0x98, 0x00, 0x00};
	u32 ncyl;
	u32 n;

	/* assume 0xff heads, 15krpm. */
	memcpy(ptr, geo_m_pg, sizeof(geo_m_pg));
	ncyl = sec >> 14; /* 256 * 64 */
	memcpy(&n, ptr+1, sizeof(u32));
	n = n | cpu_to_be32(ncyl);
	memcpy(ptr+1, &n, sizeof(u32));
	return sizeof(geo_m_pg);
}

static void build_mode_sense_response(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);
	struct tio *tio = cmnd->tio;
	u8 *data, *scb = req->scb;
	int len = 4, err = 0;
	u8 pcode;

	/* changeable parameter mode pages are unsupported */
	if ((scb[2] & 0xc0) >> 6 == 0x1)
		goto set_sense;

	pcode = req->scb[2] & 0x3f;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);

	if (LUReadonly(cmnd->lun))
		data[2] = 0x80;

	if ((scb[1] & 0x8))
		data[3] = 0;
	else {
		data[3] = 8;
		len += 8;
		*(u32 *)(data + 4) = (cmnd->lun->blk_cnt >> 32) ?
			cpu_to_be32(0xffffffff) : cpu_to_be32(cmnd->lun->blk_cnt);
		*(u32 *)(data + 8) = cpu_to_be32(1 << cmnd->lun->blk_shift);
	}

	switch (pcode) {
	case 0x0:
		break;
	case 0x2:
		len += insert_disconnect_pg(data + len);
		break;
	case 0x3:
		len += insert_format_m_pg(data + len, 1 << cmnd->lun->blk_shift);
		break;
	case 0x4:
		len += insert_geo_m_pg(data + len, cmnd->lun->blk_cnt);
		break;
	case 0x8:
		len += insert_caching_pg(data + len, LUWCache(cmnd->lun),
					 LURCache(cmnd->lun));
		break;
	case 0xa:
		len += insert_ctrl_m_pg(data + len);
		break;
	case 0x1c:
		len += insert_iec_m_pg(data + len);
		break;
	case 0x3f:
		len += insert_disconnect_pg(data + len);
		len += insert_format_m_pg(data + len, 1 << cmnd->lun->blk_shift);
		len += insert_geo_m_pg(data + len, cmnd->lun->blk_cnt);
		len += insert_caching_pg(data + len, LUWCache(cmnd->lun),
					 LURCache(cmnd->lun));
		len += insert_ctrl_m_pg(data + len);
		len += insert_iec_m_pg(data + len);
		break;
	default:
		err = -1;
	}

	if (!err) {
		data[0] = len - 1;
		tio_set(tio, len, 0);
		return;
	}

	tio_put(tio);
	cmnd->tio = NULL;
 set_sense:
	/* Invalid Field In CDB */
	iscsi_cmnd_set_sense(cmnd, ILLEGAL_REQUEST, 0x24, 0x0);
}

static void build_inquiry_response(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);
	struct tio *tio = cmnd->tio;
	u8 *data;
	u8 *scb = req->scb;
	int err = -1;

	/*
	 * - CmdDt and EVPD both set or EVPD and Page Code set: illegal
	 * - CmdDt set: not supported
	 */
	if ((scb[1] & 0x3) > 0x1 || (!(scb[1] & 0x3) && scb[2]))
		goto set_sense;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);

	if (!(scb[1] & 0x3)) {
		data[2] = 4;
		data[3] = 0x52;
		data[4] = 59;
		data[7] = 0x02;
		memset(data + 8, 0x20, 28);
		memcpy(data + 8,
			VENDOR_ID, min_t(size_t, strlen(VENDOR_ID), 8));
		memcpy(data + 16,
			PRODUCT_ID, min_t(size_t, strlen(PRODUCT_ID), 16));
		memcpy(data + 32,
			PRODUCT_REV, min_t(size_t, strlen(PRODUCT_REV), 4));
		data[58] = 0x03;
		data[59] = 0x20;
		data[60] = 0x09;
		data[61] = 0x60;
		data[62] = 0x03;
		data[63] = 0x00;
		tio_set(tio, 64, 0);
		err = 0;
	} else if (scb[1] & 0x1) {
		/* EVPD bit set */
		if (scb[2] == 0x0) {
			data[1] = 0x0;
			data[3] = 3;
			data[4] = 0x0;
			data[5] = 0x80;
			data[6] = 0x83;
			tio_set(tio, 7, 0);
			err = 0;
		} else if (scb[2] == 0x80) {
			u32 len = 4;

			if (cmnd->lun) {
				if (strlen(cmnd->lun->scsi_sn) <= 16)
					len = 16;
				else
					len = SCSI_SN_LEN;
			}

			data[1] = 0x80;
			data[3] = len;
			memset(data + 4, 0x20, len);
			if (cmnd->lun) {
				size_t offset = len -
						strlen(cmnd->lun->scsi_sn);
				memcpy(data + 4 + offset, cmnd->lun->scsi_sn,
						strlen(cmnd->lun->scsi_sn));
			}
			tio_set(tio, len + 4, 0);
			err = 0;
		} else if (scb[2] == 0x83) {
			u32 len = SCSI_ID_LEN + 8;

			data[1] = 0x83;
			data[3] = len + 4;
			data[4] = 0x1;
			data[5] = 0x1;
			data[7] = len;
			if (cmnd->lun) { /* We need this ? */
				memset(data + 8, 0x00, 8);
				memcpy(data + 8, VENDOR_ID, 
					min_t(size_t, strlen(VENDOR_ID), 8));
				memcpy(data + 16, cmnd->lun->scsi_id,
								SCSI_ID_LEN);
			}
			tio_set(tio, len + 8, 0);
			err = 0;
		}
	}

	if (!err) {
		tio_set(tio, min_t(u8, tio->size, scb[4]), 0);
		if (!cmnd->lun)
			data[0] = TYPE_NO_LUN;
		return;
	}

	tio_put(tio);
	cmnd->tio = NULL;
 set_sense:
	/* Invalid Field In CDB */
	iscsi_cmnd_set_sense(cmnd, ILLEGAL_REQUEST, 0x24, 0x0);
}

static void build_report_luns_response(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);
	struct tio *tio = cmnd->tio;
	u32 *data, size, len;
	struct iet_volume *lun;
	int rest, idx = 0;

	size = (u32)req->scb[6] << 24 | (u32)req->scb[7] << 16 |
		(u32)req->scb[8] << 8 | (u32)req->scb[9];
	if (size < 16) {
		/* Invalid Field In CDB */
		iscsi_cmnd_set_sense(cmnd, ILLEGAL_REQUEST, 0x24, 0x0);
		return;
	}

	len = atomic_read(&cmnd->conn->session->target->nr_volumes) * 8;
	size = min(size & ~(8 - 1), len + 8);

	assert(!tio);
	tio = cmnd->tio = tio_alloc(get_pgcnt(size, 0));
	tio_set(tio, size, 0);

	data = page_address(tio->pvec[idx]);
	assert(data);
	*data++ = cpu_to_be32(len);
	*data++ = 0;
	size -= 8;
	rest = PAGE_CACHE_SIZE - 8;
	list_for_each_entry(lun, &cmnd->conn->session->target->volumes, list) {
		if (lun->l_state != IDEV_RUNNING)
			continue;

		*data++ = cpu_to_be32((0x3ff & lun->lun) << 16 |
				      ((lun->lun > 0xff) ? (0x1 << 30) : 0));
		*data++ = 0;
		if ((size -= 8) == 0)
			break;
		if ((rest -= 8) == 0) {
			idx++;
			data = page_address(tio->pvec[idx]);
			rest = PAGE_CACHE_SIZE;
		}
	}
}

static void build_read_capacity_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	u32 *data;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);

	data[0] = (cmnd->lun->blk_cnt >> 32) ?
		cpu_to_be32(0xffffffff) : cpu_to_be32(cmnd->lun->blk_cnt - 1);
	data[1] = cpu_to_be32(1U << cmnd->lun->blk_shift);

	tio_set(tio, 8, 0);
}

static void build_request_sense_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	u8 *data;

	assert(!tio);
	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	memset(data, 0, 18);
	data[0] = 0xf0;
	data[1] = 0;
	data[2] = NO_SENSE;
	data[7] = 10;
	tio_set(tio, 18, 0);
}

static void build_service_action_in_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	u32 *data;
	u64 *data64;

	assert(!tio);

	/* only READ_CAPACITY_16 service action is currently supported */
	if ((cmnd_hdr(cmnd)->scb[1] & 0x1F) != 0x10) {
		/* Invalid Field In CDB */
		iscsi_cmnd_set_sense(cmnd, ILLEGAL_REQUEST, 0x24, 0x0);
		return;
	}

	tio = cmnd->tio = tio_alloc(1);
	data = page_address(tio->pvec[0]);
	assert(data);
	clear_page(data);
	data64 = (u64*) data;
	data64[0] = cpu_to_be64(cmnd->lun->blk_cnt - 1);
	data[2] = cpu_to_be32(1UL << cmnd->lun->blk_shift);

	tio_set(tio, 12, 0);
}

static void build_read_response(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;

	assert(tio);
	assert(cmnd->lun);

	if (tio_read(cmnd->lun, tio))
		/* Medium Error/Unrecovered Read Error */
		iscsi_cmnd_set_sense(cmnd, MEDIUM_ERROR, 0x11, 0x0);
}

static void build_write_response(struct iscsi_cmnd *cmnd)
{
	int err;
	struct tio *tio = cmnd->tio;

	assert(tio);
	assert(cmnd->lun);

	list_del_init(&cmnd->list);
	err = tio_write(cmnd->lun, tio);
	if (!err && !LUWCache(cmnd->lun))
		err = tio_sync(cmnd->lun, tio);

	if (err)
		/* Medium Error/Write Fault */
		iscsi_cmnd_set_sense(cmnd, MEDIUM_ERROR, 0x03, 0x0);
}

static void build_sync_cache_response(struct iscsi_cmnd *cmnd)
{
	assert(cmnd->lun);
	if (tio_sync(cmnd->lun, NULL))
		/* Medium Error/Write Fault */
		iscsi_cmnd_set_sense(cmnd, MEDIUM_ERROR, 0x03, 0x0);
}

static void build_generic_response(struct iscsi_cmnd *cmnd)
{
	return;
}

static void build_reserve_response(struct iscsi_cmnd *cmnd)
{
	switch (volume_reserve(cmnd->lun, cmnd->conn->session->sid)) {
	case -ENOENT:
		/* Logical Unit Not Supported (?) */
		iscsi_cmnd_set_sense(cmnd, ILLEGAL_REQUEST, 0x25, 0x0);
		break;
	case -EBUSY:
		cmnd->status = SAM_STAT_RESERVATION_CONFLICT;
		break;
	default:
		break;
	}
}

static void build_release_response(struct iscsi_cmnd *cmnd)
{
	int ret = volume_release(cmnd->lun,
				 cmnd->conn->session->sid, 0);
	switch (ret) {
	case -ENOENT:
		/* Logical Unit Not Supported (?) */
		iscsi_cmnd_set_sense(cmnd, ILLEGAL_REQUEST, 0x25, 0x0);
		break;
	case -EBUSY:
		cmnd->status = SAM_STAT_RESERVATION_CONFLICT;
		break;
	default:
		break;
	}
}

static void build_reservation_conflict_response(struct iscsi_cmnd *cmnd)
{
	cmnd->status = SAM_STAT_RESERVATION_CONFLICT;
}

static int disk_check_ua(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);
	struct ua_entry *ua;

	if (cmnd->lun && ua_pending(cmnd->conn->session, cmnd->lun->lun)) {
		switch(req->scb[0]){
		case INQUIRY:
		case REQUEST_SENSE:
			break;
		case REPORT_LUNS:
			ua = ua_get_match(cmnd->conn->session,
					  cmnd->lun->lun,
					  /* reported luns data has changed */
					  0x3f, 0x0e);
			ua_free(ua);
			break;
		default:
			ua = ua_get_first(cmnd->conn->session, cmnd->lun->lun);
			iscsi_cmnd_set_sense(cmnd, UNIT_ATTENTION, ua->asc,
					     ua->ascq);
			ua_free(ua);
			send_scsi_rsp(cmnd, build_generic_response);
			return 1;
		}
	}
	return 0;
}

static int disk_check_reservation(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);

	int ret = is_volume_reserved(cmnd->lun,
				     cmnd->conn->session->sid);
	if (ret == -EBUSY) {
		switch (req->scb[0]) {
		case INQUIRY:
		case RELEASE:
		case REPORT_LUNS:
		case REQUEST_SENSE:
		case READ_CAPACITY:
			/* allowed commands when reserved */
			break;
		case SERVICE_ACTION_IN:
			if ((cmnd_hdr(cmnd)->scb[1] & 0x1F) == 0x10)
				break;
			/* fall through */
		default:
			/* return reservation conflict for all others */
			send_scsi_rsp(cmnd,
				      build_reservation_conflict_response);
			return 1;
		}
	}

	return 0;
}

static int disk_execute_cmnd(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);

	req->opcode &= ISCSI_OPCODE_MASK;

	if (disk_check_ua(cmnd))
		return 0;

	if (disk_check_reservation(cmnd))
		return 0;

	switch (req->scb[0]) {
	case INQUIRY:
		send_data_rsp(cmnd, build_inquiry_response);
		break;
	case REPORT_LUNS:
		send_data_rsp(cmnd, build_report_luns_response);
		break;
	case READ_CAPACITY:
		send_data_rsp(cmnd, build_read_capacity_response);
		break;
	case MODE_SENSE:
		send_data_rsp(cmnd, build_mode_sense_response);
		break;
	case REQUEST_SENSE:
		send_data_rsp(cmnd, build_request_sense_response);
		break;
	case SERVICE_ACTION_IN:
		send_data_rsp(cmnd, build_service_action_in_response);
		break;
	case READ_6:
	case READ_10:
	case READ_16:
		send_data_rsp(cmnd, build_read_response);
		break;
	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
	case WRITE_VERIFY:
		send_scsi_rsp(cmnd, build_write_response);
		break;
	case SYNCHRONIZE_CACHE:
		send_scsi_rsp(cmnd, build_sync_cache_response);
		break;
	case RESERVE:
		send_scsi_rsp(cmnd, build_reserve_response);
		break;
	case RELEASE:
		send_scsi_rsp(cmnd, build_release_response);
		break;
	case START_STOP:
	case TEST_UNIT_READY:
	case VERIFY:
	case VERIFY_16:
		send_scsi_rsp(cmnd, build_generic_response);
		break;
	default:
		eprintk("%s\n", "we should not come here!");
		break;
	}

	return 0;
}

struct target_type disk_ops =
{
	.id = 0,
	.execute_cmnd = disk_execute_cmnd,
};
