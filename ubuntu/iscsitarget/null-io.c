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

enum {
	opt_blk_cnt, opt_ignore, opt_err,
};

static match_table_t tokens = {
	/* alias for compatibility with existing setups and documentation */
	{opt_blk_cnt, "sectors=%u"},
	/* but actually it is the scsi block count, now that we can
	 * specify the block size. */
	{opt_blk_cnt, "blocks=%u"},
	{opt_ignore, "scsiid=%s"},
	{opt_ignore, "scsisn=%s"},
	{opt_ignore, "blocksize=%s"},
	{opt_ignore, "type=%s"},
	{opt_err, NULL},
};

static int parse_nullio_params(struct iet_volume *volume, char *params)
{
	int err = 0;
	char *p, *q;

	while ((p = strsep(&params, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		if (!*p)
			continue;
		iet_strtolower(p);
		token = match_token(p, tokens, args);
		switch (token) {
		case opt_blk_cnt:
			q = match_strdup(&args[0]);
			if (!q)
				return -ENOMEM;
			volume->blk_cnt = simple_strtoull(q, NULL, 10);
			kfree(q);
			break;
		case opt_ignore:
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
}

static int nullio_attach(struct iet_volume *lu, char *args)
{
	int err = 0;

	if ((err = parse_nullio_params(lu, args)) < 0) {
		eprintk("%d\n", err);
		goto out;
	}

	if (!lu->blk_shift)
		lu->blk_shift = blksize_bits(IET_DEF_BLOCK_SIZE);

	/* defaults to 64 GiB */
	if (!lu->blk_cnt)
		lu->blk_cnt = 1 << (36 - lu->blk_shift);

out:
	if (err < 0)
		nullio_detach(lu);
	return err;
}

struct iotype nullio =
{
	.name = "nullio",
	.attach = nullio_attach,
	.detach = nullio_detach,
};
