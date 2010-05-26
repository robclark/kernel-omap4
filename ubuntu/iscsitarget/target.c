/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include "iscsi.h"
#include "digest.h"
#include "iscsi_dbg.h"

#define	MAX_NR_TARGETS	(1UL << 30)

static LIST_HEAD(target_list);
static DECLARE_MUTEX(target_list_sem);
static u32 next_target_id;
static u32 nr_targets;

static struct iscsi_sess_param default_session_param = {
	.initial_r2t = 1,
	.immediate_data = 1,
	.max_connections = 1,
	.max_recv_data_length = 8192,
	.max_xmit_data_length = 8192,
	.max_burst_length = 262144,
	.first_burst_length = 65536,
	.default_wait_time = 2,
	.default_retain_time = 20,
	.max_outstanding_r2t = 1,
	.data_pdu_inorder = 1,
	.data_sequence_inorder = 1,
	.error_recovery_level = 0,
	.header_digest = DIGEST_NONE,
	.data_digest = DIGEST_NONE,
	.ofmarker = 0,
	.ifmarker = 0,
	.ofmarkint = 2048,
	.ifmarkint = 2048,
};

static struct iscsi_trgt_param default_target_param = {
	.wthreads = DEFAULT_NR_WTHREADS,
	.target_type = 0,
	.queued_cmnds = DEFAULT_NR_QUEUED_CMNDS,
};

inline int target_lock(struct iscsi_target *target, int interruptible)
{
	int err = 0;

	if (interruptible)
		err = down_interruptible(&target->target_sem);
	else
		down(&target->target_sem);

	return err;
}

inline void target_unlock(struct iscsi_target *target)
{
	up(&target->target_sem);
}

static struct iscsi_target *__target_lookup_by_id(u32 id)
{
	struct iscsi_target *target;

	list_for_each_entry(target, &target_list, t_list) {
		if (target->tid == id)
			return target;
	}
	return NULL;
}

static struct iscsi_target *__target_lookup_by_name(char *name)
{
	struct iscsi_target *target;

	list_for_each_entry(target, &target_list, t_list) {
		if (!strcmp(target->name, name))
			return target;
	}
	return NULL;
}

struct iscsi_target *target_lookup_by_id(u32 id)
{
	struct iscsi_target *target;

	down(&target_list_sem);
	target = __target_lookup_by_id(id);
	up(&target_list_sem);

	return target;
}

static int target_thread_start(struct iscsi_target *target)
{
	int err;

	if ((err = nthread_start(target)) < 0)
		return err;

	if (!worker_thread_pool) {
		err = wthread_start(target->wthread_info,
				    target->trgt_param.wthreads, target->tid);
		if (err)
			nthread_stop(target);
	}

	return err;
}

static void target_thread_stop(struct iscsi_target *target)
{
	if (!worker_thread_pool)
		wthread_stop(target->wthread_info);

	nthread_stop(target);
}

static int iscsi_target_create(struct target_info *info, u32 tid)
{
	int err = -EINVAL, len;
	char *name = info->name;
	struct iscsi_target *target;

	dprintk(D_SETUP, "%u %s\n", tid, name);

	if (!(len = strlen(name))) {
		eprintk("The length of the target name is zero %u\n", tid);
		return err;
	}

	if (!try_module_get(THIS_MODULE)) {
		eprintk("Fail to get module %u\n", tid);
		return err;
	}

	target = kzalloc(sizeof(*target), GFP_KERNEL);
	if (!target) {
		err = -ENOMEM;
		goto out;
	}

	if (!worker_thread_pool) {
		target->wthread_info = kmalloc(sizeof(struct worker_thread_info), GFP_KERNEL);
		if (!target->wthread_info) {
			err = -ENOMEM;
			goto out;
		}
	}

	target->tid = info->tid = tid;

	memcpy(&target->sess_param, &default_session_param, sizeof(default_session_param));
	memcpy(&target->trgt_param, &default_target_param, sizeof(default_target_param));

	strncpy(target->name, name, sizeof(target->name) - 1);

	init_MUTEX(&target->target_sem);
	spin_lock_init(&target->session_list_lock);

	INIT_LIST_HEAD(&target->session_list);
	INIT_LIST_HEAD(&target->volumes);

	atomic_set(&target->nr_volumes, 0);

	nthread_init(target);

	if (!worker_thread_pool)
		wthread_init(target->wthread_info);
	else
		target->wthread_info = worker_thread_pool;


	if ((err = target_thread_start(target)) < 0) {
		target_thread_stop(target);
		goto out;
	}

	list_add(&target->t_list, &target_list);

	return 0;
out:
	if (!worker_thread_pool)
		kfree(target->wthread_info);
	kfree(target);
	module_put(THIS_MODULE);

	return err;
}

int target_add(struct target_info *info)
{
	u32 tid = info->tid;
	int err;

	err = down_interruptible(&target_list_sem);
	if (err < 0)
		return err;

	if (nr_targets > MAX_NR_TARGETS) {
		err = -EBUSY;
		goto out;
	}

	if (__target_lookup_by_name(info->name) || 
			(tid && __target_lookup_by_id(tid))) {
		err = -EEXIST;
		goto out;
	}

	if (!tid) {
		do {
			if (!++next_target_id)
				++next_target_id;
		} while (__target_lookup_by_id(next_target_id));

		tid = next_target_id;
	}

	err = iscsi_target_create(info, tid);
	if (!err)
		nr_targets++;
out:
	up(&target_list_sem);

	return err;
}

static void target_destroy(struct iscsi_target *target)
{
	dprintk(D_SETUP, "%u\n", target->tid);

	target_thread_stop(target);

	while (!list_empty(&target->volumes)) {
		struct iet_volume *volume;
		volume = list_entry(target->volumes.next, struct iet_volume, list);
		volume->l_state = IDEV_DEL;
		iscsi_volume_destroy(volume);
	}

	if (!worker_thread_pool)
		kfree(target->wthread_info);
	kfree(target);

	module_put(THIS_MODULE);
}

/* @locking: target_list_sem must be locked */
static int __target_del(struct iscsi_target *target)
{
	int err;

	target_lock(target, 0);

	if (!list_empty(&target->session_list)) {
		struct iscsi_session *session;

		do {
			session = list_entry(target->session_list.next,
						struct iscsi_session, list);
			err = session_del(target, session->sid);
			if (err < 0) {
				target_unlock(target);
				return err;
			}
		} while (!list_empty(&target->session_list));
	}

	list_del(&target->t_list);
	nr_targets--;

	target_unlock(target);
	target_destroy(target);

	return 0;
}

int target_del(u32 id)
{
	struct iscsi_target *target;
	int err;

	err = down_interruptible(&target_list_sem);
	if (err < 0)
		return err;

	target = __target_lookup_by_id(id);
	if (!target) {
		err = -ENOENT;
		goto out;
	}

	err = __target_del(target);
out:
	up(&target_list_sem);

	return err;
}

void target_del_all(void)
{
	struct iscsi_target *target, *tmp;
	int err;

	down(&target_list_sem);

	if (!list_empty(&target_list))
		iprintk("Removing all connections, sessions and targets\n");

	list_for_each_entry_safe(target, tmp, &target_list, t_list) {
		u32 tid = target->tid;
		err =__target_del(target);
		if (err)
			eprintk("Error deleteing target %u: %d\n", tid, err);
	}

	next_target_id = 0;

	up(&target_list_sem);
}

static void *iet_seq_start(struct seq_file *m, loff_t *pos)
{
	int err;

	/* are you sure this is to be interruptible? */
	err = down_interruptible(&target_list_sem);
	if (err < 0)
		return ERR_PTR(err);

	return seq_list_start(&target_list, *pos);
}

static void *iet_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &target_list, pos);
}

static void iet_seq_stop(struct seq_file *m, void *v)
{
	up(&target_list_sem);
}

static int iet_seq_show(struct seq_file *m, void *p)
{
	iet_show_info_t *func = (iet_show_info_t *)m->private;
	struct iscsi_target *target =
		list_entry(p, struct iscsi_target, t_list);
	int err;

	/* relly, interruptible?  I'd think target_lock(target, 0)
	 * would be more appropriate. --lge */
	err = target_lock(target, 1);
	if (err < 0)
		return err;

	seq_printf(m, "tid:%u name:%s\n", target->tid, target->name);

	func(m, target);

	target_unlock(target);

	return 0;
}

struct seq_operations iet_seq_op = {
	.start = iet_seq_start,
	.next = iet_seq_next,
	.stop = iet_seq_stop,
	.show = iet_seq_show,
};
