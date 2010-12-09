/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 *
 * This code is licenced under the GPL.
 */

#include <linux/proc_fs.h>

#include "iscsi.h"
#include "iscsi_dbg.h"

static DEFINE_SEMAPHORE(ioctl_sem);

struct proc_entries {
	const char *name;
	struct file_operations *fops;
};

static struct proc_entries iet_proc_entries[] =
{
	{"volume", &volume_seq_fops},
	{"session", &session_seq_fops},
};

static struct proc_dir_entry *proc_iet_dir;

void iet_procfs_exit(void)
{
	int i;

	if (!proc_iet_dir)
		return;

	for (i = 0; i < ARRAY_SIZE(iet_proc_entries); i++)
		remove_proc_entry(iet_proc_entries[i].name, proc_iet_dir);

	remove_proc_entry(proc_iet_dir->name, proc_iet_dir->parent);
}

int iet_procfs_init(void)
{
	int i;
	struct proc_dir_entry *ent;

	if (!(proc_iet_dir = proc_mkdir("iet", init_net.proc_net)))
		goto err;

	for (i = 0; i < ARRAY_SIZE(iet_proc_entries); i++) {
		ent = create_proc_entry(iet_proc_entries[i].name, 0, proc_iet_dir);
		if (ent)
			ent->proc_fops = iet_proc_entries[i].fops;
		else
			goto err;
	}

	return 0;

err:
	if (proc_iet_dir)
		iet_procfs_exit();

	return -ENOMEM;
}

static int get_module_info(unsigned long ptr)
{
	struct module_info info;
	int err;

	snprintf(info.version, sizeof(info.version), "%s", IET_VERSION_STRING);

	err = copy_to_user((void *) ptr, &info, sizeof(info));
	if (err)
		return -EFAULT;

	return 0;
}

static int get_conn_info(struct iscsi_target *target, unsigned long ptr)
{
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct conn_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;

	conn = conn_lookup(session, info.cid);
	if (!conn)
		return -ENOENT;

	info.cid = conn->cid;
	info.stat_sn = conn->stat_sn;
	info.exp_stat_sn = conn->exp_stat_sn;

	err = copy_to_user((void *) ptr, &info, sizeof(info));
	if (err)
		return -EFAULT;

	return 0;
}

static int add_conn(struct iscsi_target *target, unsigned long ptr)
{
	struct iscsi_session *session;
	struct conn_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;

	return conn_add(session, &info);
}

static int del_conn(struct iscsi_target *target, unsigned long ptr)
{
	struct iscsi_session *session;
	struct conn_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;

	return conn_del(session, &info);
}

static int get_session_info(struct iscsi_target *target, unsigned long ptr)
{
	struct iscsi_session *session;
	struct session_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;

	info.exp_cmd_sn = session->exp_cmd_sn;
	info.max_cmd_sn = session->max_cmd_sn;

	err = copy_to_user((void *) ptr, &info, sizeof(info));
	if (err)
		return -EFAULT;

	return 0;
}

static int add_session(struct iscsi_target *target, unsigned long ptr)
{
	struct session_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	return session_add(target, &info);
}

static int del_session(struct iscsi_target *target, unsigned long ptr)
{
	struct session_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	return session_del(target, info.sid);
}

static int add_volume(struct iscsi_target *target, unsigned long ptr)
{
	struct volume_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	return volume_add(target, &info);
}

static int del_volume(struct iscsi_target *target, unsigned long ptr)
{
	struct volume_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	return iscsi_volume_del(target, &info);
}

static int iscsi_param_config(struct iscsi_target *target, unsigned long ptr, int set)
{
	struct iscsi_param_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	err = iscsi_param_set(target, &info, set);
	if (err < 0 || set)
		return err;

	err = copy_to_user((void *) ptr, &info, sizeof(info));
	if (err)
		return -EFAULT;

	return 0;
}

static int add_target(unsigned long ptr)
{
	struct target_info info;
	int err;

	err = copy_from_user(&info, (void *) ptr, sizeof(info));
	if (err)
		return -EFAULT;

	err = target_add(&info);
	if (err < 0)
		return err;

	err = copy_to_user((void *) ptr, &info, sizeof(info));
	if (err)
		return -EFAULT;

	return 0;
}

static long ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iscsi_target *target = NULL;
	long err;
	u32 id;

	err = down_interruptible(&ioctl_sem);
	if (err < 0)
		return err;

	if (cmd == GET_MODULE_INFO) {
		err = get_module_info(arg);
		goto done;
	}

	if (cmd == ADD_TARGET) {
		err = add_target(arg);
		goto done;
	}

	err = get_user(id, (u32 *) arg);
	if (err < 0)
		goto done;

	/* locking handled in target_del */
	if (cmd == DEL_TARGET) {
		err = target_del(id);
		goto done;
	}

	target = target_lookup_by_id(id);
	if (!target) {
		err = -ENOENT;
		goto done;
	}

	err = target_lock(target, 1);
	if (err < 0)
		goto done;

	switch (cmd) {
	case ADD_VOLUME:
		err = add_volume(target, arg);
		break;

	case DEL_VOLUME:
		err = del_volume(target, arg);
		break;

	case ADD_SESSION:
		err = add_session(target, arg);
		break;

	case DEL_SESSION:
		err = del_session(target, arg);
		break;

	case GET_SESSION_INFO:
		err = get_session_info(target, arg);
		break;

	case ISCSI_PARAM_SET:
		err = iscsi_param_config(target, arg, 1);
		break;

	case ISCSI_PARAM_GET:
		err = iscsi_param_config(target, arg, 0);
		break;

	case ADD_CONN:
		err = add_conn(target, arg);
		break;

	case DEL_CONN:
		err = del_conn(target, arg);
		break;

	case GET_CONN_INFO:
		err = get_conn_info(target, arg);
		break;
	default:
		eprintk("invalid ioctl cmd %x\n", cmd);
		err = -EINVAL;
	}

	target_unlock(target);
done:
	up(&ioctl_sem);

	return err;
}

static int release(struct inode *i __attribute__((unused)),
		   struct file *f __attribute__((unused)))
{
	down(&ioctl_sem);
	target_del_all();
	up(&ioctl_sem);

	return 0;
}

struct file_operations ctr_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= ioctl,
	.compat_ioctl	= ioctl,
	.release	= release
};
