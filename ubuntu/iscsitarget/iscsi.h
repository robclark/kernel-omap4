/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 * Copyright (C) 2008 Arne Redlich <agr@powerkom-dd.de>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef __ISCSI_H__
#define __ISCSI_H__

#include <linux/completion.h>
#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <net/sock.h>

#include "iscsi_hdr.h"
#include "iet_u.h"

#define IET_SENSE_BUF_SIZE      18

struct iscsi_sess_param {
	int initial_r2t;
	int immediate_data;
	int max_connections;
	int max_recv_data_length;
	int max_xmit_data_length;
	int max_burst_length;
	int first_burst_length;
	int default_wait_time;
	int default_retain_time;
	int max_outstanding_r2t;
	int data_pdu_inorder;
	int data_sequence_inorder;
	int error_recovery_level;
	int header_digest;
	int data_digest;
	int ofmarker;
	int ifmarker;
	int ofmarkint;
	int ifmarkint;
};

struct iscsi_trgt_param {
	int wthreads;
	int target_type;
	int queued_cmnds;
	int nop_interval;
	int nop_timeout;
};

struct tio {
	u32 pg_cnt;

	pgoff_t idx;
	u32 offset;
	u32 size;

	struct page **pvec;

	atomic_t count;
};

struct network_thread_info {
	struct task_struct *task;
	unsigned long flags;
	struct list_head active_conns;

	spinlock_t nthread_lock;

	void (*old_state_change)(struct sock *);
	void (*old_data_ready)(struct sock *, int);
	void (*old_write_space)(struct sock *);
};

struct worker_thread_info;

struct worker_thread {
	struct task_struct *w_task;
	struct list_head w_list;
	struct worker_thread_info *w_info;
};

struct worker_thread_info {
	spinlock_t wthread_lock;

	u32 nr_running_wthreads;

	struct list_head wthread_list;
	struct list_head work_queue;

	wait_queue_head_t wthread_sleep;
};

struct iscsi_cmnd;

struct target_type {
	int id;
	int (*execute_cmnd) (struct iscsi_cmnd *);
};

enum iscsi_device_state {
	IDEV_RUNNING,
	IDEV_DEL,
};

struct iscsi_target {
	struct list_head t_list;
	u32 tid;

	char name[ISCSI_NAME_LEN];

	struct iscsi_sess_param sess_param;
	struct iscsi_trgt_param trgt_param;

	atomic_t nr_volumes;
	struct list_head volumes;
	struct list_head session_list;

	/* Prevents races between add/del session and adding UAs */
	spinlock_t session_list_lock;

	struct network_thread_info nthread_info;
	/* Points either to own list or global pool */
	struct worker_thread_info * wthread_info;

	struct semaphore target_sem;
	struct completion *done;
};

struct iscsi_queue {
	spinlock_t queue_lock;
	struct iscsi_cmnd *ordered_cmnd;
	struct list_head wait_list;
	int active_cnt;
};

struct iet_volume {
	u32 lun;

	enum iscsi_device_state l_state;
	atomic_t l_count;

	struct iscsi_target *target;
	struct list_head list;

	struct iscsi_queue queue;

	u8 scsi_id[SCSI_ID_LEN];
	u8 scsi_sn[SCSI_SN_LEN];

	u32 blk_shift;
	u64 blk_cnt;

	u64 reserve_sid;
	spinlock_t reserve_lock;

	unsigned long flags;

	struct iotype *iotype;
	void *private;
};

enum lu_flags {
	LU_READONLY,
	LU_WCACHE,
	LU_RCACHE,
};

#define LUReadonly(lu) test_bit(LU_READONLY, &(lu)->flags)
#define SetLUReadonly(lu) set_bit(LU_READONLY, &(lu)->flags)

#define LUWCache(lu) test_bit(LU_WCACHE, &(lu)->flags)
#define SetLUWCache(lu) set_bit(LU_WCACHE, &(lu)->flags)
#define ClearLUWCache(lu) clear_bit(LU_WCACHE, &(lu)->flags)

#define LURCache(lu) test_bit(LU_RCACHE, &(lu)->flags)
#define SetLURCache(lu) set_bit(LU_RCACHE, &(lu)->flags)
#define ClearLURCache(lu) clear_bit(LU_RCACHE, &(lu)->flags)

#define IET_HASH_ORDER		8
#define	cmnd_hashfn(itt)	hash_long((itt), IET_HASH_ORDER)

#define UA_HASH_LEN 8

struct iscsi_session {
	struct list_head list;
	struct iscsi_target *target;
	struct completion *done;
	char *initiator;
	u64 sid;

	u32 exp_cmd_sn;
	u32 max_cmd_sn;

	struct iscsi_sess_param param;
	u32 max_queued_cmnds;

	struct list_head conn_list;

	struct list_head pending_list;

	spinlock_t cmnd_hash_lock;
	struct list_head cmnd_hash[1 << IET_HASH_ORDER];

	spinlock_t ua_hash_lock;
	struct list_head ua_hash[UA_HASH_LEN];

	u32 next_ttt;
};

enum connection_state_bit {
	CONN_ACTIVE,
	CONN_CLOSING,
	CONN_WSPACE_WAIT,
	CONN_NEED_NOP_IN,
};

#define ISCSI_CONN_IOV_MAX	(((256 << 10) >> PAGE_SHIFT) + 1)

struct iscsi_conn {
	struct list_head list;			/* list entry in session list */
	struct iscsi_session *session;		/* owning session */

	u16 cid;
	unsigned long state;

	u32 stat_sn;
	u32 exp_stat_sn;

	int hdigest_type;
	int ddigest_type;

	struct list_head poll_list;

	struct file *file;
	struct socket *sock;
	spinlock_t list_lock;
	atomic_t nr_cmnds;
	atomic_t nr_busy_cmnds;
	struct list_head pdu_list;		/* in/outcoming pdus */
	struct list_head write_list;		/* list of data pdus to be sent */
	struct timer_list nop_timer;

	struct iscsi_cmnd *read_cmnd;
	struct msghdr read_msg;
	struct iovec read_iov[ISCSI_CONN_IOV_MAX];
	u32 read_size;
	u32 read_overflow;
	int read_state;

	struct iscsi_cmnd *write_cmnd;
	struct iovec write_iov[ISCSI_CONN_IOV_MAX];
	struct iovec *write_iop;
	struct tio *write_tcmnd;
	u32 write_size;
	u32 write_offset;
	int write_state;

	struct hash_desc rx_hash;
	struct hash_desc tx_hash;
	struct scatterlist hash_sg[ISCSI_CONN_IOV_MAX];
};

struct iscsi_pdu {
	struct iscsi_hdr bhs;
	void *ahs;
	unsigned int ahssize;
	unsigned int datasize;
};

typedef void (iet_show_info_t)(struct seq_file *seq, struct iscsi_target *target);

struct iscsi_cmnd {
	struct list_head list;
	struct list_head conn_list;
	unsigned long flags;
	struct iscsi_conn *conn;
	struct iet_volume *lun;

	struct iscsi_pdu pdu;
	struct list_head pdu_list;

	struct list_head hash_list;

	struct tio *tio;

	u8 status;

	struct timer_list timer;

	u32 r2t_sn;
	u32 r2t_length;
	u32 is_unsolicited_data;
	u32 target_task_tag;
	u32 outstanding_r2t;

	u32 hdigest;
	u32 ddigest;

	struct iscsi_cmnd *req;

	unsigned char sense_buf[IET_SENSE_BUF_SIZE];
};

struct ua_entry {
	struct list_head entry;
	struct iscsi_session *session; /* only used for debugging ATM */
	u32 lun;
	u8 asc;
	u8 ascq;
};

#define ISCSI_OP_SCSI_REJECT	ISCSI_OP_VENDOR1_CMD
#define ISCSI_OP_PDU_REJECT	ISCSI_OP_VENDOR2_CMD
#define ISCSI_OP_DATA_REJECT	ISCSI_OP_VENDOR3_CMD
#define ISCSI_OP_SCSI_ABORT	ISCSI_OP_VENDOR4_CMD

/* iscsi.c */
extern unsigned long worker_thread_pool_size;
extern struct iscsi_cmnd *cmnd_alloc(struct iscsi_conn *, int);
extern void cmnd_rx_start(struct iscsi_cmnd *);
extern void cmnd_rx_end(struct iscsi_cmnd *);
extern void cmnd_tx_start(struct iscsi_cmnd *);
extern void cmnd_tx_end(struct iscsi_cmnd *);
extern void cmnd_release(struct iscsi_cmnd *, int);
extern void send_data_rsp(struct iscsi_cmnd *, void (*)(struct iscsi_cmnd *));
extern void send_scsi_rsp(struct iscsi_cmnd *, void (*)(struct iscsi_cmnd *));
extern void iscsi_cmnd_set_sense(struct iscsi_cmnd *, u8 sense_key, u8 asc,
				 u8 ascq);
extern void send_nop_in(struct iscsi_conn *);

/* conn.c */
extern struct iscsi_conn *conn_lookup(struct iscsi_session *, u16);
extern int conn_add(struct iscsi_session *, struct conn_info *);
extern int conn_del(struct iscsi_session *, struct conn_info *);
extern void conn_del_all(struct iscsi_session *);
extern int conn_free(struct iscsi_conn *);
extern void conn_close(struct iscsi_conn *);
extern void conn_info_show(struct seq_file *, struct iscsi_session *);

/* nthread.c */
extern int nthread_init(struct iscsi_target *);
extern int nthread_start(struct iscsi_target *);
extern int nthread_stop(struct iscsi_target *);
extern void __nthread_wakeup(struct network_thread_info *);
extern void nthread_wakeup(struct iscsi_target *);

/* wthread.c */
extern int wthread_init(struct worker_thread_info *info);
extern int wthread_start(struct worker_thread_info *info, int wthreads, u32 tid);
extern int wthread_stop(struct worker_thread_info *info);
extern void wthread_queue(struct iscsi_cmnd *);
extern struct target_type *target_type_array[];
extern int wthread_module_init(void);
extern void wthread_module_exit(void);
extern struct worker_thread_info *worker_thread_pool;

/* target.c */
extern int target_lock(struct iscsi_target *, int);
extern void target_unlock(struct iscsi_target *);
struct iscsi_target *target_lookup_by_id(u32);
extern int target_add(struct target_info *);
extern int target_del(u32 id);
extern void target_del_all(void);
extern struct seq_operations iet_seq_op;

/* config.c */
extern int iet_procfs_init(void);
extern void iet_procfs_exit(void);
extern int iet_info_show(struct seq_file *, iet_show_info_t *);

/* session.c */
extern struct file_operations session_seq_fops;
extern struct iscsi_session *session_lookup(struct iscsi_target *, u64);
extern int session_add(struct iscsi_target *, struct session_info *);
extern int session_del(struct iscsi_target *, u64);
extern void session_del_all(struct iscsi_target *);

/* volume.c */
extern struct file_operations volume_seq_fops;
extern int volume_add(struct iscsi_target *, struct volume_info *);
extern int iscsi_volume_del(struct iscsi_target *, struct volume_info *);
extern void iscsi_volume_destroy(struct iet_volume *);
extern struct iet_volume *volume_lookup(struct iscsi_target *, u32);
extern struct iet_volume *volume_get(struct iscsi_target *, u32);
extern void volume_put(struct iet_volume *);
extern int volume_reserve(struct iet_volume *volume, u64 sid);
extern int volume_release(struct iet_volume *volume, u64 sid, int force);
extern int is_volume_reserved(struct iet_volume *volume, u64 sid);

/* tio.c */
extern int tio_init(void);
extern void tio_exit(void);
extern struct tio *tio_alloc(int);
extern void tio_get(struct tio *);
extern void tio_put(struct tio *);
extern void tio_set(struct tio *, u32, loff_t);
extern int tio_read(struct iet_volume *, struct tio *);
extern int tio_write(struct iet_volume *, struct tio *);
extern int tio_sync(struct iet_volume *, struct tio *);

/* iotype.c */
extern struct iotype *get_iotype(const char *name);
extern void put_iotype(struct iotype *iot);

/* params.c */
extern int iscsi_param_set(struct iscsi_target *, struct iscsi_param_info *, int);

/* target_disk.c */
extern struct target_type disk_ops;

/* event.c */
extern int event_send(u32, u64, u32, u32, int);
extern int event_init(void);
extern void event_exit(void);

/* ua.c */
int ua_init(void);
void ua_exit(void);
struct ua_entry * ua_get_first(struct iscsi_session *, u32 lun);
struct ua_entry * ua_get_match(struct iscsi_session *, u32 lun, u8 asc,
			       u8 ascq);
void ua_free(struct ua_entry *);
int ua_pending(struct iscsi_session *, u32 lun);
void ua_establish_for_session(struct iscsi_session *, u32 lun, u8 asc,
			     u8 ascq);
void ua_establish_for_other_sessions(struct iscsi_session *, u32 lun, u8 asc,
				     u8 ascq);
void ua_establish_for_all_sessions(struct iscsi_target *, u32 lun, u8 asc,
				   u8 ascq);

#define get_pgcnt(size, offset)	((((size) + ((offset) & ~PAGE_CACHE_MASK)) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)

static inline void iscsi_cmnd_get_length(struct iscsi_pdu *pdu)
{
#if defined(__BIG_ENDIAN)
	pdu->ahssize = pdu->bhs.length.ahslength * 4;
	pdu->datasize = pdu->bhs.length.datalength;
#elif defined(__LITTLE_ENDIAN)
	pdu->ahssize = (pdu->bhs.length & 0xff) * 4;
	pdu->datasize = be32_to_cpu(pdu->bhs.length & ~0xff);
#else
#error
#endif
}

static inline void iscsi_cmnd_set_length(struct iscsi_pdu *pdu)
{
#if defined(__BIG_ENDIAN)
	pdu->bhs.length.ahslength = pdu->ahssize / 4;
	pdu->bhs.length.datalength = pdu->datasize;
#elif defined(__LITTLE_ENDIAN)
	pdu->bhs.length = cpu_to_be32(pdu->datasize) | (pdu->ahssize / 4);
#else
#error
#endif
}

#define cmnd_hdr(cmnd) ((struct iscsi_scsi_cmd_hdr *) (&((cmnd)->pdu.bhs)))
#define cmnd_ttt(cmnd) cpu_to_be32((cmnd)->pdu.bhs.ttt)
#define cmnd_itt(cmnd) cpu_to_be32((cmnd)->pdu.bhs.itt)
#define cmnd_opcode(cmnd) ((cmnd)->pdu.bhs.opcode & ISCSI_OPCODE_MASK)
#define cmnd_scsicode(cmnd) cmnd_hdr(cmnd)->scb[0]

#define	SECTOR_SIZE_BITS	9

enum cmnd_flags {
	CMND_hashed,
	CMND_queued,
	CMND_final,
	CMND_waitio,
	CMND_close,
	CMND_lunit,
	CMND_pending,
	CMND_tmfabort,
	CMND_rxstart,
	CMND_timer_active,
};

#define set_cmnd_hashed(cmnd)	set_bit(CMND_hashed, &(cmnd)->flags)
#define cmnd_hashed(cmnd)	test_bit(CMND_hashed, &(cmnd)->flags)

#define set_cmnd_queued(cmnd)	set_bit(CMND_queued, &(cmnd)->flags)
#define cmnd_queued(cmnd)	test_bit(CMND_queued, &(cmnd)->flags)

#define set_cmnd_final(cmnd)	set_bit(CMND_final, &(cmnd)->flags)
#define cmnd_final(cmnd)	test_bit(CMND_final, &(cmnd)->flags)

#define set_cmnd_waitio(cmnd)	set_bit(CMND_waitio, &(cmnd)->flags)
#define cmnd_waitio(cmnd)	test_bit(CMND_waitio, &(cmnd)->flags)

#define set_cmnd_close(cmnd)	set_bit(CMND_close, &(cmnd)->flags)
#define cmnd_close(cmnd)	test_bit(CMND_close, &(cmnd)->flags)

#define set_cmnd_lunit(cmnd)	set_bit(CMND_lunit, &(cmnd)->flags)
#define cmnd_lunit(cmnd)	test_bit(CMND_lunit, &(cmnd)->flags)

#define set_cmnd_pending(cmnd)	set_bit(CMND_pending, &(cmnd)->flags)
#define clear_cmnd_pending(cmnd)	clear_bit(CMND_pending, &(cmnd)->flags)
#define cmnd_pending(cmnd)	test_bit(CMND_pending, &(cmnd)->flags)

#define set_cmnd_tmfabort(cmnd)	set_bit(CMND_tmfabort, &(cmnd)->flags)
#define cmnd_tmfabort(cmnd)	test_bit(CMND_tmfabort, &(cmnd)->flags)

#define set_cmnd_rxstart(cmnd)	set_bit(CMND_rxstart, &(cmnd)->flags)
#define cmnd_rxstart(cmnd)	test_bit(CMND_rxstart, &(cmnd)->flags)

#define set_cmnd_timer_active(cmnd)  set_bit(CMND_timer_active, &(cmnd)->flags)
#define clear_cmnd_timer_active(cmnd) \
	                        clear_bit(CMND_timer_active, &(cmnd)->flags)
#define cmnd_timer_active(cmnd) test_bit(CMND_timer_active, &(cmnd)->flags)

#define VENDOR_ID	"IET"
#define PRODUCT_ID	"VIRTUAL-DISK"
#define PRODUCT_REV	"0"

#endif	/* __ISCSI_H__ */
