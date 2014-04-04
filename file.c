/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */


#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/aio.h>
#include <linux/kernel.h>
#include "kkv.h"

#define MAX_TOKENS 8
#define TOKEN_COMMAND 0
#define TOKEN_KEY 1
#define TOKEN_VALUE 2

#define COMMAND_GET "get"
#define COMMAND_SET "set"
#define COMMAND_ADD "add"
#define COMMAND_REPLACE "replace"
#define COMMAND_DELETE "delete"
#define COMMAND_SHRINK "shrink"

struct kkv_request {
	char *command;
	char *key;
	ssize_t nkey;
	char *value;
	ssize_t nvalue;
};

#ifdef DEBUG_KKV_STAT
static ssize_t used_mem = 0;
static ssize_t freed_mem = 0;

ssize_t used_memory_in_file(void)
{
	return used_mem;
}

ssize_t freed_memory_in_file(void)
{
	return freed_mem;
}
#endif

#define KKV_REQ_BUF_SIZE (2 * PAGE_SIZE)

static char *kkv_req_buffer;

int prepare_in_file(void)
{
	kkv_req_buffer = kmalloc(KKV_REQ_BUF_SIZE, GFP_KERNEL);
#ifdef DEBUG_KKV_STAT
	used_mem += KKV_REQ_BUF_SIZE;
#endif
	return kkv_req_buffer ? 0 : -ENOMEM;
}

void clean_in_file(void)
{
	kfree(kkv_req_buffer);
#ifdef DEBUG_KKV_STAT
	freed_mem += KKV_REQ_BUF_SIZE;
#endif
}

static ssize_t kkv_aio_read(struct kiocb *iocb, const struct iovec *iov,
	unsigned long nr_segs, loff_t pos)
{
	struct file *filp = iocb->ki_filp;
	ssize_t ret;
	size_t count;
	loff_t *ppos = &iocb->ki_pos;
	struct address_space *mapping;

	count = 0;
	ret = generic_segment_checks(iov, &nr_segs, &count, VERIFY_WRITE);
	if (ret)
		goto out;

	if (!count) {
		ret = -EFBIG;
		goto out;
	}

	mapping = filp->f_mapping;
	ret = mapping->a_ops->direct_IO(READ, iocb,
		iov, pos, nr_segs);
	if (ret > 0) {
		*ppos = pos + ret;
		// count -= ret;
	}

	//if (ret < 0 || !count || *ppos >= size) {
	//  file_accessed(filp);
	// }
out:
	return ret;
}

static ssize_t __kkv_aio_write(struct kiocb *iocb, const struct iovec *iov,
	unsigned long nr_segs, loff_t *ppos)
{
	struct file *file = iocb->ki_filp;
	struct address_space * mapping = file->f_mapping;
	size_t ocount; /* original count */
	size_t count; /* after file limit checks */
	struct inode *inode = mapping->host;
	loff_t pos;
	ssize_t ret;

	ret = file_remove_suid(file);
	if (ret)
		goto out;
	ret = file_update_time(file);
	if (ret)
		goto out;

	ocount = 0;
	ret = generic_segment_checks(iov, &nr_segs, &ocount, VERIFY_READ);
	if (ret)
		return ret;

	pos = *ppos;
	count = ocount;
	ret = generic_write_checks(file, &pos, &count, 0);
	if (ret)
		goto out;
	if (!count) {
		ret = -EFBIG;
		goto out;
	}

	if (count != ocount)
		nr_segs = iov_shorten((struct iovec *) iov, nr_segs, count);

	ret = mapping->a_ops->direct_IO(WRITE, iocb, iov, pos, nr_segs);

	if (ret > 0) {
		*ppos = pos + ret;
		if (pos > i_size_read(inode)) {
			i_size_write(inode, *ppos);
		}
	}
out:
	return ret;
}

static ssize_t kkv_aio_write(struct kiocb *iocb, const struct iovec *iov,
	unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	BUG_ON(iocb->ki_pos != pos);

	mutex_lock(&inode->i_mutex);
	ret = __kkv_aio_write(iocb, iov, nr_segs, &iocb->ki_pos);
	mutex_unlock(&inode->i_mutex);

	return ret;
}

/* only support text key/value currently.
 */
static int kkv_parse_cmd(char *buf, struct kkv_request *req, int rw, const struct iovec *iov, unsigned long nr_segs)
{

	int i;
	char *tokens[MAX_TOKENS];
	char *s, *e;
	char *msg_end;

	if (nr_segs != 1)
		return -1;

	//TODO: handle the scatter-gatter buffer
	__copy_from_user(buf, iov[0].iov_base, iov[0].iov_len);
	msg_end = buf + iov[0].iov_len;
	*msg_end = '\0';
#ifdef DEBUG_KKV_FS
	//printk("the msg is: %s\n", buf);
#endif
	s = e = buf;
	i = 0;
	while (e < msg_end && i < MAX_TOKENS) {
		if (*e == '\0') {
			tokens[i++] = s;
			s = e + 1;
		}
		e++;
	}

	if (i > 3) i = 3;

	switch (i) {
	case 3:
		req->value = tokens[TOKEN_VALUE];
		req->nvalue = strlen(tokens[TOKEN_VALUE]);
	case 2:
		req->key = tokens[TOKEN_KEY];
		req->nkey = strlen(tokens[TOKEN_KEY]);
	case 1:
		req->command = tokens[TOKEN_COMMAND];
		break;
	default:
		return -1;
	}
	return 0;
}

static ssize_t kkv_DIO(int rw, struct kiocb *iocb, const struct iovec *iov,
	loff_t offset, unsigned long nr_segs)
{
	ssize_t ret;
	struct kkv_request req;
	//char *value;
#ifdef DEBUG_KKV_FS
	struct file *file = iocb->ki_filp;
	printk("the file name: %s\n", file->f_dentry->d_name.name);
#endif
	ret = kkv_parse_cmd(kkv_req_buffer, &req, rw, iov, nr_segs);
	if (ret != 0) {
		return -EINVAL;
	}
#ifdef DEBUG_KKV_FS
	printk("the command is: %s\n", req.command);
#endif
	ret = -EINVAL;
	if (rw & WRITE) {

		if (!strcmp(req.command, COMMAND_SET)) {
#ifdef DEBUG_KKV_FS
			printk("the key is: %s\n", req.key);
			//printk("the value is: %s\n", req.value);
#endif
			ret = engine_set(req.key, req.nkey, req.value, req.nvalue);
		} else if (!strcmp(req.command, COMMAND_ADD)) {
#ifdef DEBUG_KKV_FS
			printk("the key is: %s\n", req.key);
			//printk("the value is: %s\n", req.value);
#endif
			ret = engine_add(req.key, req.nkey, req.value, req.nvalue);
		} else if (!strcmp(req.command, COMMAND_REPLACE)) {
#ifdef DEBUG_KKV_FS
			printk("the key is: %s\n", req.key);
			//printk("the value is: %s\n", req.value);
#endif
			ret = engine_replace(req.key, req.nkey, req.value, req.nvalue);
		} else if (!strcmp(req.command, COMMAND_DELETE)) {
#ifdef DEBUG_KKV_FS
			printk("the key is: %s\n", req.key);
#endif
			ret = engine_delete(req.key, req.nkey);
		} else if (!strcmp(req.command, COMMAND_SHRINK)) {
			ret = engine_shrink();
		}
	} else {
#ifdef DEBUG_KKV_FS
		printk("the key is: %s\n", req.key);
#endif
		if (!strcmp(req.command, COMMAND_GET)) {
			//ret = engine_get(req.key, req.nkey, &value);
			ret = engine_get(req.key, req.nkey, kkv_req_buffer, KKV_REQ_BUF_SIZE);
			if (ret > 0) {
				//if (ret > iov[0].iov_len)
					//ret = iov[0].iov_len;
				__copy_to_user(iov[0].iov_base, kkv_req_buffer, ret);
			}
		}
	}
	return ret;
}

static int kkv_set_page_dirty_no_writeback(struct page *page)
{
	if (!PageDirty(page))
		return !TestSetPageDirty(page);
	return 0;
}


/*
 * kkv_aops & kkv_file_operations will be exported.
 */
const struct address_space_operations kkv_aops = {
	.readpage = simple_readpage,
	.write_begin = simple_write_begin,
	.write_end = simple_write_end,
	.set_page_dirty = kkv_set_page_dirty_no_writeback,
	.direct_IO = kkv_DIO,
};

const struct file_operations kkv_file_operations = {
	.read = do_sync_read,
	.aio_read = kkv_aio_read,
	.write = do_sync_write,
	.aio_write = kkv_aio_write,
	.mmap = generic_file_mmap,
	.fsync = noop_fsync,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write,
	.llseek = generic_file_llseek,
};
