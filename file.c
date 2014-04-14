/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/aio.h>
#include <linux/kernel.h>

#include "kkv.h"


#define COMMAND_CONFIG 0
#define COMMAND_DECONFIG 1
#define COMMAND_GET 10
#define COMMAND_SET 11
#define COMMAND_ADD 12
#define COMMAND_REPLACE 13
#define COMMAND_DELETE 14
#define COMMAND_SHRINK 15


typedef struct {
    __u32 id;
    __u32 command;
    __u32 key_len;
    __u32 value_len;
    char data[0];
} kkv_packet;



struct kkv_request {
    __u32 command;
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

struct kkv_file_info {
    char kkv_req_buffer[KKV_REQ_BUF_SIZE];
};

static int kkv_open(struct inode *inode, struct file *file)
{
    struct kkv_file_info *kfi;

    kfi = kmalloc(sizeof(struct kkv_file_info), GFP_KERNEL);
#ifdef DEBUG_KKV_STAT
    used_mem += KKV_REQ_BUF_SIZE;
#endif
    file->private_data = kfi;

    return kfi ? 0 : -ENOMEM;
}

static int kkv_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
#ifdef DEBUG_KKV_STAT
    freed_mem += KKV_REQ_BUF_SIZE;
#endif
    return 0;
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
static int kkv_parse_request(char *buf, struct kkv_request *req, int rw, const struct iovec *iov, unsigned long nr_segs)
{
    kkv_packet *pk;

    if (nr_segs != 1)
        return -1;

    //TODO: handle the scatter-gatter buffer
    __copy_from_user(buf, iov[0].iov_base, iov[0].iov_len);

    pk=(kkv_packet*)buf;
    req->command=pk->command;
    req->nkey=pk->key_len;
    req->nvalue=pk->value_len ? pk->value_len : (KKV_REQ_BUF_SIZE-sizeof(kkv_packet)-req->nkey);
    req->key=buf+sizeof(kkv_packet);
    req->value=req->key+req->nkey;

    return 0;
}

static ssize_t kkv_create_response(char *buf, struct kkv_request *req, const struct iovec *iov, unsigned long nr_segs)
{
    ssize_t len;
    kkv_packet *pk;
    pk=(kkv_packet*)buf;
    pk->key_len=req->nkey;
    pk->value_len=req->nvalue;
    len=sizeof(kkv_packet)+req->nkey+req->nvalue;
    __copy_to_user(iov[0].iov_base,buf,len);
    return len;
}

static ssize_t kkv_DIO(int rw, struct kiocb *iocb, const struct iovec *iov,
                       loff_t offset, unsigned long nr_segs)
{
    ssize_t ret;
    struct kkv_request req;
    struct file *file = iocb->ki_filp;
    char *kkv_req_buffer=file->private_data;

#ifdef DEBUG_KKV_FS
    printk("the file name: %s\n", file->f_dentry->d_name.name);
#endif

    ret = kkv_parse_request(kkv_req_buffer, &req, rw, iov, nr_segs);
    if (ret != 0) {
        return -EINVAL;
    }

#ifdef DEBUG_KKV_FS
	printk("the command is: %d\n", req.command);
	printk("the nkey is: %ld\n", req.nkey);
	printk("the key is: %s\n", req.key);
	printk("the nvalue is: %ld\n", req.nvalue);
    printk("the value is: %s\n", req.value);
#endif

    ret = -EINVAL;
    switch (req.command) {
    case COMMAND_GET:
        ret = engine_get(req.key, req.nkey, req.value, req.nvalue);
        if (ret > 0) {
            req.nvalue=ret;
            kkv_create_response(kkv_req_buffer, &req, iov, nr_segs);
        }
        break;

    case COMMAND_SET:
        ret = engine_set(req.key, req.nkey, req.value, req.nvalue);
        break;

    case COMMAND_ADD:
        ret = engine_add(req.key, req.nkey, req.value, req.nvalue);
        break;

    case COMMAND_REPLACE:
        ret = engine_replace(req.key, req.nkey, req.value, req.nvalue);
        break;

    case COMMAND_DELETE:
        ret = engine_delete(req.key, req.nkey);
        break;

    case COMMAND_SHRINK:
        ret = engine_shrink();
        break;

    default:
        ret=0;
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
    .open=kkv_open,
    .release=kkv_release,
    .fsync = noop_fsync,
    .splice_read = generic_file_splice_read,
    .splice_write = generic_file_splice_write,
    .llseek = generic_file_llseek,
};
