/*
 * Yama Linux Security Module
 *
 * Author: Kees Cook <kees.cook@canonical.com>
 *
 * Copyright (C) 2010 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/security.h>
#include <linux/sysctl.h>
#include <linux/ptrace.h>
#include <linux/ratelimit.h>

static int ptrace_scope = 1;
static int protected_sticky_symlinks = 1;
static int protected_nonaccess_hardlinks = 1;

/**
 * yama_ptrace_access_check - validate PTRACE_ATTACH calls
 * @child: child task pointer
 * @mode: ptrace attach mode
 *
 * Returns 0 if following the ptrace is allowed, -ve on error.
 */
static int yama_ptrace_access_check(struct task_struct *child,
				    unsigned int mode)
{
	int rc;

	rc = cap_ptrace_access_check(child, mode);
	if (rc != 0)
		return rc;

	/* require ptrace target be a child of ptracer on attach */
	if (mode == PTRACE_MODE_ATTACH && ptrace_scope &&
	    !capable(CAP_SYS_PTRACE)) {
		struct task_struct *walker = child;

		rcu_read_lock();
		read_lock(&tasklist_lock);
		while (walker->pid > 0) {
			if (walker == current)
				break;
			walker = walker->real_parent;
		}
		if (walker->pid == 0)
			rc = -EPERM;
		read_unlock(&tasklist_lock);
		rcu_read_unlock();
	}

	if (rc) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_INFO "ptrace of non-child"
			" pid %d was attempted by: %s (pid %d)\n",
			child->pid,
			get_task_comm(name, current),
			current->pid);
	}

	return rc;
}

/**
 * yama_inode_follow_link - check for symlinks in sticky world-writeable dirs
 * @dentry: The inode/dentry of the symlink
 * @nameidata: The path data of the symlink
 *
 * In the case of the protected_sticky_symlinks sysctl being enabled,
 * CAP_DAC_OVERRIDE needs to be specifically ignored if the symlink is
 * in a sticky world-writable directory.  This is to protect privileged
 * processes from failing races against path names that may change out
 * from under them by way of other users creating malicious symlinks.
 * It will permit symlinks to only be followed when outside a sticky
 * world-writable directory, or when the uid of the symlink and follower
 * match, or when the directory owner matches the symlink's owner.
 *
 * Returns 0 if following the symlink is allowed, -ve on error.
 */
static int yama_inode_follow_link(struct dentry *dentry,
				  struct nameidata *nameidata)
{
	int rc = 0;
	const struct inode *parent;
	const struct inode *inode;
	const struct cred *cred;

	if (!protected_sticky_symlinks)
		return 0;

	/* owner and follower match? */
	cred = current_cred();
	inode = dentry->d_inode;
	if (cred->fsuid == inode->i_uid)
		return 0;

	/* check parent directory mode and owner */
	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent->d_inode;
	if ((parent->i_mode & (S_ISVTX|S_IWOTH)) == (S_ISVTX|S_IWOTH) &&
	    parent->i_uid != inode->i_uid) {
		rc = -EACCES;
	}
	spin_unlock(&dentry->d_lock);

	if (rc) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_NOTICE "non-matching-uid symlink "
			"following attempted in sticky world-writable "
			"directory by %s (fsuid %d != %d)\n",
			get_task_comm(name, current),
			cred->fsuid, inode->i_uid);
	}

	return rc;
}

/**
 * yama_path_link - verify that hardlinking is allowed
 * @old_dentry: the source inode/dentry to hardlink from
 * @new_dir: target directory
 * @new_dentry: the target inode/dentry to hardlink to
 *
 * Block hardlink when all of:
 *  - fsuid does not match inode
 *  - not CAP_FOWNER
 *  - and at least one of:
 *    - inode is not a regular file
 *    - inode is setuid
 *    - inode is setgid and group-exec
 *    - access failure for read and write
 *
 * Returns 0 if successful, -ve on error.
 */
static int yama_path_link(struct dentry *old_dentry, struct path *new_dir,
			  struct dentry *new_dentry)
{
	int rc = 0;
	struct inode *inode = old_dentry->d_inode;
	const int mode = inode->i_mode;
	const struct cred *cred = current_cred();

	if (!protected_nonaccess_hardlinks)
		return 0;

	if (cred->fsuid != inode->i_uid &&
	    (!S_ISREG(mode) || (mode & S_ISUID) ||
	     ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) ||
	     (generic_permission(inode, MAY_READ | MAY_WRITE, NULL))) &&
	    !capable(CAP_FOWNER)) {
		char name[sizeof(current->comm)];
		printk_ratelimited(KERN_INFO "non-accessible hardlink"
			" creation was attempted by: %s (fsuid %d)\n",
			get_task_comm(name, current),
			cred->fsuid);
		rc = -EPERM;
	}

	return rc;
}

static struct security_operations yama_ops = {
	.name =			"yama",

	.ptrace_access_check =	yama_ptrace_access_check,
	.inode_follow_link =	yama_inode_follow_link,
	.path_link =		yama_path_link,
};

#ifdef CONFIG_SYSCTL
static int zero;
static int one = 1;

struct ctl_path yama_sysctl_path[] = {
	{ .procname = "kernel", },
	{ .procname = "yama", },
	{ }
};

static struct ctl_table yama_sysctl_table[] = {
	{
		.procname       = "protected_sticky_symlinks",
		.data           = &protected_sticky_symlinks,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{
		.procname       = "protected_nonaccess_hardlinks",
		.data           = &protected_nonaccess_hardlinks,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{
		.procname       = "ptrace_scope",
		.data           = &ptrace_scope,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

static __init int yama_init(void)
{
	if (!security_module_enable(&yama_ops))
		return 0;

	printk(KERN_INFO "Yama: becoming mindful.\n");

	if (register_security(&yama_ops))
		panic("Yama: kernel registration failed.\n");

#ifdef CONFIG_SYSCTL
	if (!register_sysctl_paths(yama_sysctl_path, yama_sysctl_table))
		panic("Yama: sysctl registration failed.\n");
#endif

	return 0;
}

security_initcall(yama_init);
