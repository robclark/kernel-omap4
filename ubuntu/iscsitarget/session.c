/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"

struct iscsi_session *session_lookup(struct iscsi_target *target, u64 sid)
{
	struct iscsi_session *session;

	list_for_each_entry(session, &target->session_list, list) {
		if (session->sid == sid)
			return session;
	}
	return NULL;
}

static struct iscsi_session *
iet_session_alloc(struct iscsi_target *target, struct session_info *info)
{
	int i;
	struct iscsi_session *session;
	struct iet_volume *vol;

	dprintk(D_SETUP, "%p %u %#Lx\n", target, target->tid,
		(unsigned long long) info->sid);

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return NULL;

	session->target = target;
	session->sid = info->sid;
	memcpy(&session->param, &target->sess_param, sizeof(session->param));
	session->max_queued_cmnds = target->trgt_param.queued_cmnds;

	session->exp_cmd_sn = info->exp_cmd_sn;
	session->max_cmd_sn = info->max_cmd_sn;

	session->initiator = kstrdup(info->initiator_name, GFP_KERNEL);
	if (!session->initiator) {
		kfree(session);
		return NULL;
	}

	INIT_LIST_HEAD(&session->conn_list);
	INIT_LIST_HEAD(&session->pending_list);

	spin_lock_init(&session->cmnd_hash_lock);
	for (i = 0; i < ARRAY_SIZE(session->cmnd_hash); i++)
		INIT_LIST_HEAD(&session->cmnd_hash[i]);

	spin_lock_init(&session->ua_hash_lock);
	for (i = 0; i < ARRAY_SIZE(session->ua_hash); i++)
		INIT_LIST_HEAD(&session->ua_hash[i]);

	list_for_each_entry(vol, &target->volumes, list)
		/* power-on, reset, or bus device reset occurred */
		ua_establish_for_session(session, vol->lun, 0x29, 0x0);

	session->next_ttt = 1;

	spin_lock(&target->session_list_lock);
	list_add(&session->list, &target->session_list);
	spin_unlock(&target->session_list_lock);

	return session;
}

static int session_free(struct iscsi_session *session)
{
	int i;
	struct ua_entry *ua, *tmp;
	struct list_head *l;
	struct iscsi_target *target = session->target;

	dprintk(D_SETUP, "%#Lx\n", (unsigned long long) session->sid);

	spin_lock(&target->session_list_lock);

	assert(list_empty(&session->conn_list));

	for (i = 0; i < ARRAY_SIZE(session->cmnd_hash); i++) {
		if (!list_empty(&session->cmnd_hash[i]))
			BUG();
	}

	for (i = 0; i < ARRAY_SIZE(session->ua_hash); i++) {
		l = &session->ua_hash[i];
		list_for_each_entry_safe(ua, tmp, l, entry) {
			list_del_init(&ua->entry);
			ua_free(ua);
		}
	}

	list_del(&session->list);

	kfree(session->initiator);
	kfree(session);

	spin_unlock(&target->session_list_lock);

	return 0;
}

int session_add(struct iscsi_target *target, struct session_info *info)
{
	struct iscsi_session *session;

	session = session_lookup(target, info->sid);
	if (session)
		return -EEXIST;

	session = iet_session_alloc(target, info);
	if (!session)
		return -ENOMEM;

	return 0;
}

int session_del(struct iscsi_target *target, u64 sid)
{
	struct iscsi_session *session;

	session = session_lookup(target, sid);
	if (!session)
		return -ENOENT;

	if (!list_empty(&session->conn_list)) {
		eprintk("%llu still have connections\n", (unsigned long long) session->sid);
		return -EBUSY;
	}

	return session_free(session);
}

void session_del_all(struct iscsi_target *target)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct iscsi_session *sess;

	while (!list_empty(&target->session_list)) {
		init_completion(&done);
		target_lock(target, 0);
		sess = list_entry(target->session_list.next, struct
				  iscsi_session, list);
		sess->done = &done;
		conn_del_all(sess);
		target_unlock(target);
		wait_for_completion(&done);
		target_lock(target, 0);
		session_free(sess);
		target_unlock(target);
	}

	if (target->done)
		complete(target->done);
}

static void iet_session_info_show(struct seq_file *seq, struct iscsi_target *target)
{
	struct iscsi_session *session;

	list_for_each_entry(session, &target->session_list, list) {
		seq_printf(seq, "\tsid:%llu initiator:%s\n",
			   (unsigned long long) session->sid, session->initiator);
		conn_info_show(seq, session);
	}
}

static int iet_session_seq_open(struct inode *inode, struct file *file)
{
	int res;
	res = seq_open(file, &iet_seq_op);
	if (!res)
		((struct seq_file *)file->private_data)->private =
			iet_session_info_show;
	return res;
}

struct file_operations session_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= iet_session_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
