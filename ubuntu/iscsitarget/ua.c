/*
 * IET Unit Attention support
 *
 * Copyright (C) 2009 Xie Gang <xiegang112@gmail.com>
 * Copyright (C) 2009 Arne Redlich <arne.redlich@googlemail.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <scsi/scsi.h>

#include "iscsi.h"
#include "iscsi_dbg.h"

#define ua_hashfn(lun) ((lun % UA_HASH_LEN))

static struct kmem_cache *ua_cache;

int ua_init(void)
{
	ua_cache = KMEM_CACHE(ua_entry, 0);
	if (!ua_cache) {
		eprintk("%s", "Failed to create ua cache\n");
		return -ENOMEM;
	}

	return 0;
}

void ua_exit(void)
{
	if (ua_cache)
		kmem_cache_destroy(ua_cache);
}

/* sess->ua_hash_lock needs to be held */
static struct ua_entry * ua_find_hash(struct iscsi_session *sess, u32 lun,
				      u8 asc, u8 ascq, int match)
{
	struct ua_entry *ua;
	struct list_head *h = &sess->ua_hash[ua_hashfn(lun)];

	list_for_each_entry(ua, h, entry) {
		if (ua->lun == lun) {
			if (!match)
				return ua;
			if (ua->asc == asc && ua->ascq == ascq)
				return ua;
		}
	}

	return NULL;
}

int ua_pending(struct iscsi_session *sess, u32 lun)
{
	struct ua_entry *ua;

	spin_lock(&sess->ua_hash_lock);
	ua = ua_find_hash(sess, lun, 0, 0, 0);
	spin_unlock(&sess->ua_hash_lock);

	dprintk_ua(ua, sess, lun);

	return ua ? 1 : 0;
}

/* sess->ua_hash_lock needs to be held */
static struct ua_entry * __ua_get_hash(struct iscsi_session *sess, u32 lun,
				       u8 asc, u8 ascq, int match)
{
	struct ua_entry *ua = ua_find_hash(sess, lun, asc, ascq, match);

	if (ua)
		list_del_init(&ua->entry);

	return ua;
}

struct ua_entry * ua_get_first(struct iscsi_session *sess, u32 lun)
{
	struct ua_entry *ua;

	spin_lock(&sess->ua_hash_lock);
	ua = __ua_get_hash(sess, lun, 0, 0, 0);
	spin_unlock(&sess->ua_hash_lock);

	dprintk_ua(ua, sess, lun);

	return ua;
}

struct ua_entry * ua_get_match(struct iscsi_session *sess, u32 lun,
			       u8 asc, u8 ascq)
{
	struct ua_entry *ua;

	spin_lock(&sess->ua_hash_lock);
	ua = __ua_get_hash(sess, lun, asc, ascq, 1);
	spin_unlock(&sess->ua_hash_lock);

	dprintk_ua(ua, sess, lun);

	return ua;
}

void ua_establish_for_session(struct iscsi_session *sess, u32 lun,
			      u8 asc, u8 ascq)
{
	struct list_head *l = &sess->ua_hash[ua_hashfn(lun)];
	struct ua_entry *ua = kmem_cache_alloc(ua_cache, GFP_KERNEL);

	if (!ua) {
		eprintk("%s", "Failed to alloc ua");
		return;
	}

	ua->asc = asc;
	ua->ascq = ascq;
	ua->lun = lun;
	ua->session = sess;

	spin_lock(&sess->ua_hash_lock);
	list_add_tail(&ua->entry, l);
	spin_unlock(&sess->ua_hash_lock);

	dprintk_ua(ua, sess, lun);
}

void ua_establish_for_other_sessions(struct iscsi_session *sess, u32 lun,
				     u8 asc, u8 ascq)
{
	struct list_head *l = &sess->target->session_list;
	struct iscsi_session *s;

	spin_lock(&sess->target->session_list_lock);
	list_for_each_entry(s, l, list)
		if (s->sid != sess->sid)
			ua_establish_for_session(s, lun, asc, ascq);
	spin_unlock(&sess->target->session_list_lock);
}

void ua_establish_for_all_sessions(struct iscsi_target *target, u32 lun,
				   u8 asc, u8 ascq)
{
	struct list_head *l = &target->session_list;
	struct iscsi_session *s;

	spin_lock(&target->session_list_lock);
	list_for_each_entry(s, l, list)
		ua_establish_for_session(s, lun, asc, ascq);
	spin_unlock(&target->session_list_lock);

}

void ua_free(struct ua_entry *ua)
{
	if (!ua)
		return;

	dprintk_ua(ua, ua->session, ua->lun);
	BUG_ON(!list_empty(&ua->entry));
	kmem_cache_free(ua_cache, ua);
}
