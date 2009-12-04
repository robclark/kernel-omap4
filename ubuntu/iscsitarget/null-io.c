/*
 * Target device null I/O.
 * (C) 2005 MING Zhang <mingz@ele.uri.edu>
 * This code is licenced under the GPL.
 *
 * The nullio mode will not return any meaningful or previous written
 * data. It is only for performance measurement purpose.
 */

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/writeback.h>

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "iotype.h"

struct nullio_data {
	u64 sectors;
};

enum {
	Opt_sectors, Opt_ignore, Opt_err,
};

static match_table_t tokens = {
	{Opt_sectors, "Sectors=%u"},
	{Opt_ignore, "Type=%s"},
	{Opt_err, NULL},
};

static int parse_nullio_params(struct iet_volume *volume, char *params)
{
	int err = 0;
	char *p, *q;
	struct nullio_data *data = volume->private;

	while ((p = strsep(&params, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_sectors:
			q = match_strdup(&args[0]);
			if (!q)
				return -ENOMEM;
			data->sectors = simple_strtoull(q, NULL, 10);
			kfree(q);
			break;
		case Opt_ignore:
			break;
		default:
			eprintk("Unknown %s\n", p);
			return -EINVAL;
			break;
		}
	}
	return err;
}

static void nullio_detach(struct iet_volume *lu)
{
	struct nullio_data *p = lu->private;

	kfree(p);
	lu->private = NULL;
}

static int nullio_attach(struct iet_volume *lu, char *args)
{
	int err = 0;
	struct nullio_data *p;

	if (lu->private) {
		printk("already attached ? %d\n", lu->lun);
		return -EBUSY;
	}

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	lu->private = p;

	if ((err = parse_nullio_params(lu, args)) < 0) {
		eprintk("%d\n", err);
		goto out;
	}

	lu->blk_shift = SECTOR_SIZE_BITS;
	lu->blk_cnt = (p->sectors = p->sectors ? : 1 << 27); /* 64 GB */

out:
	if (err < 0)
		nullio_detach(lu);
	return err;
}

void nullio_show(struct iet_volume *lu, struct seq_file *seq)
{
	struct nullio_data *p = lu->private;
	seq_printf(seq, " sectors:%llu\n", p->sectors);
}

struct iotype nullio =
{
	.name = "nullio",
	.attach = nullio_attach,
	.detach = nullio_detach,
	.show = nullio_show,
};
