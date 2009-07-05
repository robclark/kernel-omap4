/*
 * Compressed RAM based swap device
 *
 * Copyright (C) 2008, 2009  Nitin Gupta
 *
 * This RAM based block device acts as swap disk.
 * Pages swapped to this device are compressed and
 * stored in memory.
 *
 * Released under the terms of GNU General Public License Version 2.0
 *
 * Project home: http://compcache.googlecode.com
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/lzo.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/vmalloc.h>

#include "compat.h"
#include "ramzswap.h"

/* Globals */
static struct ramzswap rzs;
static struct ramzswap_stats stats;
/*
 * Pages that compress to larger than this size are
 * forwarded to backing swap, if present or stored
 * uncompressed in memory otherwise.
 */
static unsigned int MAX_CPAGE_SIZE;

/* Module params (documentation at end) */
static unsigned long disksize_kb;
static unsigned long memlimit_kb;
static char *backing_swap;

static int __init ramzswap_init(void);
static struct block_device_operations ramzswap_devops = {
	.owner = THIS_MODULE,
};

static int test_flag(u32 index, enum rzs_pageflags flag)
{
	return rzs.table[index].flags & BIT(flag);
}

static void set_flag(u32 index, enum rzs_pageflags flag)
{
	rzs.table[index].flags |= BIT(flag);
}

static void clear_flag(u32 index, enum rzs_pageflags flag)
{
	rzs.table[index].flags &= ~BIT(flag);
}

static int page_zero_filled(void *ptr)
{
	u32 pos;
	u64 *page;

	page = (u64 *)ptr;

	for (pos = 0; pos != PAGE_SIZE / sizeof(*page); pos++) {
		if (page[pos])
			return 0;
	}

	return 1;
}

/*
 * Given <pagenum, offset> pair, provide a dereferencable pointer.
 */
static void *get_ptr_atomic(u32 pagenum, u16 offset, enum km_type type)
{
	unsigned char *page;

	page = kmap_atomic(pfn_to_page(pagenum), type);
	return page + offset;
}

static void put_ptr_atomic(void *ptr, enum km_type type)
{
	kunmap_atomic(ptr, type);
}

#if defined(STATS)
static struct proc_dir_entry *proc;

static int proc_ramzswap_read(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int len;
	size_t succ_writes, mem_used;
	unsigned int good_compress_perc = 0, no_compress_perc = 0;

	mem_used = xv_get_total_size_bytes(rzs.mem_pool)
			+ (stats.pages_expand << PAGE_SHIFT);

	if (off > 0) {
		*eof = 1;
		return 0;
	}

#define K(x)	((x) >> 10)
	/* Basic stats */
	len = sprintf(page,
		"DiskSize:	%8zu kB\n",
		(size_t)(K(rzs.disksize)));

	if (rzs.backing_swap) {
		/* This must always be less than ComprDataSize */
		len += sprintf(page + len,
			"MemLimit:	%8zu kB\n",
			K(rzs.memlimit));
	}

	succ_writes = stats.num_writes - stats.failed_writes;

	if (succ_writes && stats.pages_stored) {
		good_compress_perc = stats.good_compress * 100
					/ stats.pages_stored;
		no_compress_perc = stats.pages_expand * 100
					/ stats.pages_stored;
	}

	/* Extended stats */
	len += sprintf(page + len,
		"NumReads:	%8llu\n"
		"NumWrites:	%8llu\n"
		"FailedReads:	%8llu\n"
		"FailedWrites:	%8llu\n"
		"InvalidIO:	%8llu\n"
		"PagesDiscard:	%8llu\n"
		"ZeroPages:	%8u\n"
		"GoodCompress:	%8u %%\n"
		"NoCompress:	%8u %%\n"
		"PagesStored:	%8u\n"
		"PagesUsed:	%8zu\n"
		"OrigDataSize:	%8zu kB\n"
		"ComprDataSize:	%8zu kB\n"
		"MemUsedTotal:	%8zu kB\n",
		stats.num_reads,
		stats.num_writes,
		stats.failed_reads,
		stats.failed_writes,
		stats.invalid_io,
		stats.pages_discard,
		stats.pages_zero,
		good_compress_perc,
		no_compress_perc,
		stats.pages_stored,
		mem_used >> PAGE_SHIFT,
		(size_t)(K(stats.pages_stored << PAGE_SHIFT)),
		(size_t)(K(stats.compr_size)),
		(size_t)(K(mem_used)));

	if (rzs.backing_swap) {
		/* This must always be less than ComprDataSize */
		len += sprintf(page + len,
			"BDevNumReads:	%8llu\n"
			"BDevNumWrites:	%8llu\n",
			stats.bdev_num_reads,
			stats.bdev_num_writes);
	}

	return len;
}
#endif	/* STATS */

/*
 * Check if value of backing_swap module param is sane.
 * Claim this device and set ramzswap size equal to
 * size of this block device.
 */
static int setup_backing_swap(void)
{
	int error = 0;
	struct inode *inode;
	struct file *swap_file;
	struct address_space *mapping;
	struct block_device *bdev = NULL;

	if (backing_swap == NULL) {
		pr_debug(C "backing_swap param not given\n");
		goto out;
	}

	pr_info(C "Using backing swap device: %s\n", backing_swap);

	swap_file = filp_open(backing_swap, O_RDWR | O_LARGEFILE, 0);
	if (IS_ERR(swap_file)) {
		pr_err(C "Error opening backing device: %s\n", backing_swap);
		error = -EINVAL;
		goto out;
	}

	mapping = swap_file->f_mapping;
	inode = mapping->host;

	if (S_ISBLK(inode->i_mode)) {
		bdev = I_BDEV(inode);
		error = bd_claim(bdev, ramzswap_init);
		if (error < 0) {
			bdev = NULL;
			goto bad_param;
		}
		rzs.old_block_size = block_size(bdev);
		error = set_blocksize(bdev, PAGE_SIZE);
		if (error < 0)
			goto bad_param;
	} else {
		/* TODO: support for regular file as backing swap */
		pr_info(C "%s is not a block device.\n", backing_swap);
		error = -EINVAL;
		goto out;
	}

	rzs.swap_file = swap_file;
	rzs.backing_swap = bdev;
	rzs.disksize = i_size_read(inode);
	BUG_ON(!rzs.disksize);

	return 0;

bad_param:
	if (bdev) {
		set_blocksize(bdev, rzs.old_block_size);
		bd_release(bdev);
	}
	filp_close(swap_file, NULL);

out:
	rzs.backing_swap = NULL;
	return error;
}

/*
 * Check if request is within bounds and page aligned.
 */
static inline int valid_swap_request(struct bio *bio)
{
	if (unlikely(
		(bio->bi_sector >= (rzs.disksize >> SECTOR_SHIFT)) ||
		(bio->bi_sector & (SECTORS_PER_PAGE - 1)) ||
		(bio->bi_vcnt != 1) ||
		(bio->bi_size != PAGE_SIZE) ||
		(bio->bi_io_vec[0].bv_offset != 0))) {

		return 0;
	}

	/* swap request is valid */
	return 1;
}

static void ramzswap_free_page(size_t index)
{
	u32 clen;
	void *obj;

	u32 pagenum = rzs.table[index].pagenum;
	u32 offset = rzs.table[index].offset;

	if (unlikely(test_flag(index, RZS_UNCOMPRESSED))) {
		clen = PAGE_SIZE;
		__free_page(pfn_to_page(pagenum));
		clear_flag(index, RZS_UNCOMPRESSED);
		stat_dec(stats.pages_expand);
		goto out;
	}

	obj = get_ptr_atomic(pagenum, offset, KM_USER0);
	clen = xv_get_object_size(obj) - sizeof(struct zobj_header);
	put_ptr_atomic(obj, KM_USER0);

	xv_free(rzs.mem_pool, pagenum, offset);
	stat_dec_if_less(stats.good_compress, clen, PAGE_SIZE / 2 + 1);

out:
	stats.compr_size -= clen;
	stat_dec(stats.pages_stored);

	rzs.table[index].pagenum = 0;
	rzs.table[index].offset = 0;
}

#ifdef SWAP_DISCARD_SUPPORTED
static int ramzswap_prepare_discard(struct request_queue *q,
					struct request *req)
{
	return 0;
}

/*
 * Called by main I/O handler function. This helper
 * function handles 'discard' I/O requests which means
 * that  some swap pages are no longer required, so
 * swap device can take needed action -- we free memory
 * allocated for these pages.
 */
static int ramzswap_discard(struct bio *bio)
{
	size_t index, start_page, num_pages;

	start_page = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;
	num_pages = bio->bi_size >> (SECTOR_SHIFT + SECTORS_PER_PAGE_SHIFT);

	for (index = start_page; index < start_page + num_pages; index++) {
		if (rzs.table[index].pagenum) {
			ramzswap_free_page(index);
			stat_inc(stats.pages_discard);
		}
	}

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	BIO_ENDIO(bio, 0);
	return 0;
}
#endif

int handle_zero_page(struct bio *bio)
{
	void *user_mem;
	struct page *page = bio->bi_io_vec[0].bv_page;

	user_mem = get_ptr_atomic(page_to_pfn(page), 0, KM_USER0);
	memset(user_mem, 0, PAGE_SIZE);
	put_ptr_atomic(user_mem, KM_USER0);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	BIO_ENDIO(bio, 0);
	return 0;
}

int handle_uncompressed_page(struct bio *bio)
{
	u32 index;
	struct page *page;
	unsigned char *user_mem, *cmem;

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >>SECTORS_PER_PAGE_SHIFT;

	user_mem = get_ptr_atomic(page_to_pfn(page), 0, KM_USER0);
	cmem = get_ptr_atomic(rzs.table[index].pagenum,
			rzs.table[index].offset, KM_USER1);

	memcpy(user_mem, cmem, PAGE_SIZE);
	put_ptr_atomic(user_mem, KM_USER0);
	put_ptr_atomic(cmem, KM_USER1);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	BIO_ENDIO(bio, 0);
	return 0;
}


/*
 * Called when request page is not present in ramzswap.
 * Its either in backing swap device (if present) or
 * this is an attempt to read before any previous write
 * to this location - this happens due to readahead when
 * swap device is read from user-space (e.g. during swapon)
 */
int handle_ramzswap_fault(struct bio *bio)
{
	void *user_mem;
	struct page *page = bio->bi_io_vec[0].bv_page;

	/*
	 * Always forward such requests to backing swap
	 * device (if present)
	 */
	if (rzs.backing_swap) {
		stat_dec(stats.num_reads);
		stat_inc(stats.bdev_num_reads);
		bio->bi_bdev = rzs.backing_swap;
		return 1;
	}

	/*
	 * Its unlikely event in case backing dev is
	 * not present
	 */
	pr_debug(C "Read before write on swap device: "
		"sector=%lu, size=%u, offset=%u\n",
		(ulong)(bio->bi_sector), bio->bi_size,
		bio->bi_io_vec[0].bv_offset);
	user_mem = kmap(page);
	memset(user_mem, 0, PAGE_SIZE);
	kunmap(page);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	BIO_ENDIO(bio, 0);
	return 0;
}

int ramzswap_read(struct bio *bio)
{
	int ret;
	u32 index;
	size_t clen;
	struct page *page;
	struct zobj_header *zheader;
	unsigned char *user_mem, *cmem;

	stat_inc(stats.num_reads);

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;

	if (test_flag(index, RZS_ZERO))
		return handle_zero_page(bio);

	/* Requested page is not present in compressed area */
	if (!rzs.table[index].pagenum)
		return handle_ramzswap_fault(bio);

	/* Page is stored uncompressed since its incompressible */
	if (unlikely(test_flag(index, RZS_UNCOMPRESSED)))
		return handle_uncompressed_page(bio);

	user_mem = get_ptr_atomic(page_to_pfn(page), 0, KM_USER0);
	clen = PAGE_SIZE;

	cmem = get_ptr_atomic(rzs.table[index].pagenum,
			rzs.table[index].offset, KM_USER1);

	ret = lzo1x_decompress_safe(
		cmem + sizeof(*zheader),
		xv_get_object_size(cmem) - sizeof(*zheader),
		user_mem, &clen);

	put_ptr_atomic(user_mem, KM_USER0);
	put_ptr_atomic(cmem, KM_USER1);

	/* should NEVER happen */
	if (unlikely(ret != LZO_E_OK)) {
		pr_err(C "Decompression failed! err=%d, page=%u\n",
			ret, index);
		stat_inc(stats.failed_reads);
		goto out;
	}

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	BIO_ENDIO(bio, 0);
	return 0;

out:
	BIO_IO_ERROR(bio);
	return 0;
}

int ramzswap_write(struct bio *bio)
{
	int ret, fwd_write_request = 0;
	u32 offset;
	size_t clen, index;
	struct zobj_header *zheader;
	struct page *page, *page_store;
	unsigned char *user_mem, *cmem, *src;

	stat_inc(stats.num_writes);

	page = bio->bi_io_vec[0].bv_page;
	index = bio->bi_sector >> SECTORS_PER_PAGE_SHIFT;

	src = rzs.compress_buffer;

	/*
	 * System swaps to same sector again when the stored page
	 * is no longer referenced by any process. So, its now safe
	 * to free the memory that was allocated for this page.
	 */
	if (rzs.table[index].pagenum)
		ramzswap_free_page(index);

	/*
	 * No memory ia allocated for zero filled pages.
	 * Simply clear zero page flag.
	 */
	if (test_flag(index, RZS_ZERO)) {
		stat_dec(stats.pages_zero);
		clear_flag(index, RZS_ZERO);
	}

	mutex_lock(&rzs.lock);

	user_mem = get_ptr_atomic(page_to_pfn(page), 0, KM_USER0);
	if (page_zero_filled(user_mem)) {
		put_ptr_atomic(user_mem, KM_USER0);
		mutex_unlock(&rzs.lock);
		stat_inc(stats.pages_zero);
		set_flag(index, RZS_ZERO);

		set_bit(BIO_UPTODATE, &bio->bi_flags);
		BIO_ENDIO(bio, 0);
		return 0;
	}

	if (rzs.backing_swap &&
		(stats.compr_size > rzs.memlimit - PAGE_SIZE)) {
		put_ptr_atomic(user_mem, KM_USER0);
		mutex_unlock(&rzs.lock);
		fwd_write_request = 1;
		goto out;
	}

	ret = lzo1x_1_compress(user_mem, PAGE_SIZE, src, &clen,
				rzs.compress_workmem);

	put_ptr_atomic(user_mem, KM_USER0);

	if (unlikely(ret != LZO_E_OK)) {
		mutex_unlock(&rzs.lock);
		pr_err(C "Compression failed! err=%d\n", ret);
		stat_inc(stats.failed_writes);
		goto out;
	}

	/*
	 * Page is incompressible. Forward it to backing swap
	 * if present. Otherwise, store it as-is (uncompressed)
	 * since we do not want to return too many swap write
	 * errors which has side effect of hanging the system.
	 */
	if (unlikely(clen > MAX_CPAGE_SIZE)) {
		if (rzs.backing_swap) {
			mutex_unlock(&rzs.lock);
			fwd_write_request = 1;
			goto out;
		}

		clen = PAGE_SIZE;
		page_store = alloc_page(GFP_NOIO | __GFP_HIGHMEM);
		if (unlikely(!page_store)) {
			mutex_unlock(&rzs.lock);
			stat_inc(stats.failed_writes);
			goto out;
		}

		offset = 0;
		set_flag(index, RZS_UNCOMPRESSED);
		stat_inc(stats.pages_expand);
		rzs.table[index].pagenum = page_to_pfn(page_store);
		src = get_ptr_atomic(page_to_pfn(page), 0, KM_USER0);
		goto memstore;
	}

	if (xv_malloc(rzs.mem_pool, clen + sizeof(*zheader),
			&rzs.table[index].pagenum, &offset,
			GFP_NOIO | __GFP_HIGHMEM)) {
		mutex_unlock(&rzs.lock);
		pr_info(C "Error allocating memory for compressed "
			"page: %zu, size=%zu\n", index, clen);
		stat_inc(stats.failed_writes);
		if (rzs.backing_swap)
			fwd_write_request = 1;
		goto out;
	}

memstore:
	rzs.table[index].offset = offset;

	cmem = get_ptr_atomic(rzs.table[index].pagenum,
			rzs.table[index].offset, KM_USER1);

#if 0
	/* Back-reference needed for memory defragmentation */
	if (!test_flag(index, RZS_UNCOMPRESSED)) {
		zheader = (struct zobj_header *)cmem;
		zheader->table_idx = index;
		cmem += sizeof(*zheader);
	}
#endif

	memcpy(cmem, src, clen);

	put_ptr_atomic(cmem, KM_USER1);
	if (unlikely(test_flag(index, RZS_UNCOMPRESSED)))
		put_ptr_atomic(src, KM_USER0);

	/* Update stats */
	stats.compr_size += clen;
	stat_inc(stats.pages_stored);
	stat_inc_if_less(stats.good_compress, clen, PAGE_SIZE / 2 + 1);

	mutex_unlock(&rzs.lock);

	set_bit(BIO_UPTODATE, &bio->bi_flags);
	BIO_ENDIO(bio, 0);
	return 0;

out:
	if (fwd_write_request) {
		stat_inc(stats.bdev_num_writes);
		bio->bi_bdev = rzs.backing_swap;
		return 1;
	}

	BIO_IO_ERROR(bio);
	return 0;
}

/*
 * Handler function for all ramzswap I/O requests.
 */
static int ramzswap_make_request(struct request_queue *queue, struct bio *bio)
{
	int ret = 0;

#ifdef SWAP_DISCARD_SUPPORTED
	if (bio_discard(bio))
		return ramzswap_discard(bio);
#endif

	if (!valid_swap_request(bio)) {
		stat_inc(stats.invalid_io);
		BIO_IO_ERROR(bio);
		return 0;
	}

	switch (bio_data_dir(bio)) {
	case READ:
		ret = ramzswap_read(bio);
		break;

	case WRITE:
		ret = ramzswap_write(bio);
		break;
	}

	return ret;
}

/*
 * Swap header (1st page of swap device) contains information
 * to indentify it as a swap partition. Prepare such a header
 * for ramzswap device (ramzswap0) so that swapon can identify
 * it as swap partition. In case backing swap device is provided,
 * copy its swap header.
 */
static int setup_swap_header(union swap_header *s)
{
	int ret = 0;
	struct page *page;
	struct address_space *mapping;
	union swap_header *backing_swap_header;

	/*
	 * There is no backing swap device. Create a swap header
	 * that is acceptable by swapon.
	 */
	if (rzs.backing_swap == NULL) {
		s->info.version = 1;
		s->info.last_page = rzs.disksize >> PAGE_SHIFT;
		s->info.nr_badpages = 0;
		memcpy(s->magic.magic, "SWAPSPACE2", 10);
		return 0;
	}

	/*
	 * We have a backing swap device. Copy its swap header
	 * to ramzswap device header. If this header contains
	 * invalid information (backing device not a swap
	 * partition, etc.), swapon will fail for ramzswap
	 * which is correct behavior - we don't want to swap
	 * over filesystem partition!
	 */

	/* Read the backing swap header (code from sys_swapon) */
	mapping = rzs.swap_file->f_mapping;
	if (!mapping->a_ops->readpage) {
		ret = -EINVAL;
		goto out;
	}

	page = read_mapping_page(mapping, 0, rzs.swap_file);
	if (IS_ERR(page)) {
		ret = PTR_ERR(page);
		goto out;
	}

	backing_swap_header = kmap(page);
	*s = *backing_swap_header;
	kunmap(page);

out:
	return ret;
}

static void ramzswap_set_disksize(size_t totalram_bytes)
{
	rzs.disksize = disksize_kb << 10;

	if (!disksize_kb) {
		pr_info(C
		"disk size not provided. You can use disksize_kb module "
		"param to specify size.\nUsing default: (%u%% of RAM).\n",
		DEFAULT_DISKSIZE_PERC_RAM
		);
		rzs.disksize = DEFAULT_DISKSIZE_PERC_RAM *
					(totalram_bytes / 100);
	}

	if (disksize_kb > 2 * (totalram_bytes >> 10)) {
		pr_info(C
		"There is little point creating a ramzswap of greater than "
		"twice the size of memory since we expect a 2:1 compression "
		"ratio. Note that ramzswap uses about 0.1%% of the size of "
		"the swap device when not in use so a huge ramzswap is "
		"wasteful.\n"
		"\tMemory Size: %zu kB\n"
		"\tSize you selected: %lu kB\n"
		"Continuing anyway ...\n",
		totalram_bytes >> 10, disksize_kb
		);
	}

	rzs.disksize &= PAGE_MASK;
	pr_info(C "disk size set to %zu kB\n", rzs.disksize >> 10);
}

/*
 * memlimit cannot be greater than backing disk size.
 */
static void ramzswap_set_memlimit(size_t totalram_bytes)
{
	int memlimit_valid = 1;
	rzs.memlimit = memlimit_kb << 10;

	if (!rzs.memlimit) {
		pr_info(C "memory limit not set. You can use "
			"memlimit_kb module param to specify limit.");
		memlimit_valid = 0;
	}

	if (rzs.memlimit > rzs.disksize) {
		pr_info(C "memory limit cannot be greater than "
			"disksize: limit=%zu, disksize=%zu",
			rzs.memlimit, rzs.disksize);
		memlimit_valid = 0;
	}

	if (!memlimit_valid) {
		size_t mempart, disksize;
		pr_info(C "\nUsing default: MIN[(%u%% of RAM), "
			"(backing disk size)].\n",
			DEFAULT_MEMLIMIT_PERC_RAM);
		mempart = DEFAULT_MEMLIMIT_PERC_RAM * (totalram_bytes / 100);
		disksize = rzs.disksize;
		rzs.memlimit = mempart > disksize ? disksize : mempart;
	}

	if (rzs.memlimit > totalram_bytes / 2) {
		pr_info(C
		"Its not advisable setting limit more than half of "
		"size of memory since we expect a 2:1 compression ratio. "
		"Limit represents amount of *compressed* data we can keep "
		"in memory!\n"
		"\tMemory Size: %zu kB\n"
		"\tLimit you selected: %lu kB\n"
		"Continuing anyway ...\n",
		totalram_bytes >> 10, memlimit_kb
		);
	}

	rzs.memlimit &= PAGE_MASK;
	BUG_ON(!rzs.memlimit);

	pr_info(C "memory limit set to %zu kB\n", rzs.memlimit >> 10);
}

static int __init ramzswap_init(void)
{
	int ret;
	size_t num_pages, totalram_bytes;
	struct sysinfo i;
	struct page *page;
	void *swap_header;

	mutex_init(&rzs.lock);

	ret = setup_backing_swap();
	if (ret)
		goto fail;

	si_meminfo(&i);
	/* Here is a trivia: guess unit used for i.totalram !! */
	totalram_bytes = i.totalram << PAGE_SHIFT;

	if (rzs.backing_swap)
		ramzswap_set_memlimit(totalram_bytes);
	else
		ramzswap_set_disksize(totalram_bytes);

	rzs.compress_workmem = kmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (rzs.compress_workmem == NULL) {
		pr_err(C "Error allocating compressor working memory\n");
		ret = -ENOMEM;
		goto fail;
	}

	rzs.compress_buffer = kmalloc(2 * PAGE_SIZE, GFP_KERNEL);
	if (rzs.compress_buffer == NULL) {
		pr_err(C "Error allocating compressor buffer space\n");
		ret = -ENOMEM;
		goto fail;
	}

	num_pages = rzs.disksize >> PAGE_SHIFT;
	rzs.table = vmalloc(num_pages * sizeof(*rzs.table));
	if (rzs.table == NULL) {
		pr_err(C "Error allocating ramzswap address table\n");
		ret = -ENOMEM;
		goto fail;
	}
	memset(rzs.table, 0, num_pages * sizeof(*rzs.table));

	page = alloc_page(__GFP_ZERO);
	if (page == NULL) {
		pr_err(C "Error allocating swap header page\n");
		ret = -ENOMEM;
		goto fail;
	}
	rzs.table[0].pagenum = page_to_pfn(page);
	set_flag(0, RZS_UNCOMPRESSED);

	swap_header = kmap(page);
	ret = setup_swap_header((union swap_header *)(swap_header));
	kunmap(page);
	if (ret) {
		pr_err(C "Error setting swap header\n");
		goto fail;
	}

	rzs.disk = alloc_disk(1);
	if (rzs.disk == NULL) {
		pr_err(C "Error allocating disk structure\n");
		ret = -ENOMEM;
		goto fail;
	}

	rzs.disk->first_minor = 0;
	rzs.disk->fops = &ramzswap_devops;
	/*
	 * It is named like this to prevent distro installers
	 * from offering ramzswap as installation target. They
	 * seem to ignore all devices beginning with 'ram'
	 */
	strcpy(rzs.disk->disk_name, "ramzswap0");

	rzs.disk->major = register_blkdev(0, rzs.disk->disk_name);
	if (rzs.disk->major < 0) {
		pr_err(C "Cannot register block device\n");
		ret = -EFAULT;
		goto fail;
	}

	rzs.disk->queue = blk_alloc_queue(GFP_KERNEL);
	if (rzs.disk->queue == NULL) {
		pr_err(C "Cannot register disk queue\n");
		ret = -EFAULT;
		goto fail;
	}

	set_capacity(rzs.disk, rzs.disksize >> SECTOR_SHIFT);
	blk_queue_make_request(rzs.disk->queue, ramzswap_make_request);

#ifdef QUEUE_FLAG_NONROT
	/*
	 * Assuming backing device is "rotational" type.
	 * TODO: check if its actually "non-rotational" (SSD).
	 *
	 * We have ident mapping of sectors for ramzswap and
	 * and the backing swap device. So, this queue flag
	 * should be according to backing dev.
	 */
	if (!rzs.backing_swap)
		queue_flag_set_unlocked(QUEUE_FLAG_NONROT, rzs.disk->queue);
#endif
#ifdef SWAP_DISCARD_SUPPORTED
	blk_queue_set_discard(rzs.disk->queue, ramzswap_prepare_discard);
#endif
	blk_queue_logical_block_size(rzs.disk->queue, PAGE_SIZE);
	add_disk(rzs.disk);

	rzs.mem_pool = xv_create_pool();
	if (!rzs.mem_pool) {
		pr_err(C "Error creating memory pool\n");
		ret = -ENOMEM;
		goto fail;
	}

#if defined(STATS)
	proc = create_proc_entry("ramzswap", S_IRUGO, NULL);
	if (proc)
		proc->read_proc = &proc_ramzswap_read;
	else {
		ret = -ENOMEM;
		pr_warning(C "Error creating proc entry\n");
		goto fail;
	}
#endif

	/*
	 * Pages that compress to size greater than this are forwarded
	 * to physical swap disk (if backing dev is provided)
	 */
	if (rzs.backing_swap)
		MAX_CPAGE_SIZE = MAX_CPAGE_SIZE_BDEV;
	else
		MAX_CPAGE_SIZE = MAX_CPAGE_SIZE_NOBDEV;

	pr_debug(C "Max compressed page size: %u bytes\n", MAX_CPAGE_SIZE);

	pr_debug(C "Initialization done!\n");
	return 0;

fail:
	if (rzs.disk != NULL) {
		if (rzs.disk->major > 0)
			unregister_blkdev(rzs.disk->major, rzs.disk->disk_name);
		del_gendisk(rzs.disk);
	}

	if (rzs.table && rzs.table[0].pagenum)
		__free_page(pfn_to_page(rzs.table[0].pagenum));
	kfree(rzs.compress_workmem);
	kfree(rzs.compress_buffer);
	vfree(rzs.table);
	xv_destroy_pool(rzs.mem_pool);
#if defined(STATS)
	if (proc)
		remove_proc_entry("ramzswap", proc->parent);
#endif
	pr_err(C "Initialization failed: err=%d\n", ret);
	return ret;
}

static void __exit ramzswap_exit(void)
{
	size_t index, num_pages;
	num_pages = rzs.disksize >> PAGE_SHIFT;

	unregister_blkdev(rzs.disk->major, rzs.disk->disk_name);
	del_gendisk(rzs.disk);

	/* Close backing swap device (if present) */
	if (rzs.backing_swap) {
		set_blocksize(rzs.backing_swap, rzs.old_block_size);
		bd_release(rzs.backing_swap);
		filp_close(rzs.swap_file, NULL);
	}

	__free_page(pfn_to_page(rzs.table[0].pagenum));
	kfree(rzs.compress_workmem);
	kfree(rzs.compress_buffer);

	/* Free all pages that are still in ramzswap */
	for (index = 1; index < num_pages; index++) {
		u32 pagenum, offset;

		pagenum = rzs.table[index].pagenum;
		offset = rzs.table[index].offset;

		if (!pagenum)
			continue;

		if (unlikely(test_flag(index, RZS_UNCOMPRESSED)))
			__free_page(pfn_to_page(pagenum));
		else
			xv_free(rzs.mem_pool, pagenum, offset);
	}

	vfree(rzs.table);
	xv_destroy_pool(rzs.mem_pool);

#if defined(STATS)
	remove_proc_entry("ramzswap", proc->parent);
#endif
	pr_debug(C "cleanup done!\n");
}

/*
 * This param is applicable only when there is no backing swap device.
 * We ignore this param in case backing dev is provided since then its
 * always equal to size of the backing swap device.
 *
 * This size refers to amount of (uncompressed) data it can hold.
 * For e.g. disksize_kb=1024 means it can hold 1024kb worth of
 * uncompressed data even if this data compresses to just, say, 100kb.
 *
 * Default value is used if this param is missing or 0 (if its applicable).
 * Default: [DEFAULT_DISKSIZE_PERC_RAM]% of RAM
 */
module_param(disksize_kb, ulong, 0);
MODULE_PARM_DESC(disksize_kb, "ramzswap device size (kB)");

/*
 * This param is applicable only when backing swap device is provided.
 * This refers to limit on amount of (compressed) data it can hold in
 * memory. Note that total amount of memory used (MemUsedTotal) can
 * exceed this memlimit since that includes memory wastage due to
 * fragmentation and metadata overhead.
 *
 * Any additional data beyond this limit is forwarded to backing
 * swap device. TODO: allow changing memlimit at runtime.
 *
 * Default value is used if this param is missing or 0 (if its applicable).
 * Default: MIN([DEFAULT_MEMLIMIT_PERC_RAM]% of RAM, Backing Device Size)
 */
module_param(memlimit_kb, ulong, 0);
MODULE_PARM_DESC(memlimit_kb, "ramzswap memory limit (kB)");

/*
 * This is block device to be used as backing store for ramzswap.
 * When pages more than memlimit_kb as swapped to ramzswap, we store
 * any additional pages in this device. We may also move some pages
 * from ramzswap to this device in case system is really low on
 * memory (TODO).
 *
 * This device is not directly visible to kernel as a swap device
 * (/proc/swaps will only show /dev/ramzswap0 and not this device).
 * Managing this backing device is the job of ramzswap module.
 */
module_param(backing_swap, charp, 0);
MODULE_PARM_DESC(backing_swap, "Backing swap partition");

module_init(ramzswap_init);
module_exit(ramzswap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Compressed RAM Based Swap Device");
