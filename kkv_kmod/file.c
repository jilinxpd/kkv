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
#include "protocol.h"


#define COMMAND_CONFIG 0
#define COMMAND_DECONFIG 1
#define COMMAND_GET 10
#define COMMAND_SET 11
#define COMMAND_ADD 12
#define COMMAND_REPLACE 13
#define COMMAND_DELETE 14
#define COMMAND_SHRINK 15

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

static ssize_t kkv_DIO(int rw, struct kiocb *iocb, const struct iovec *iov,
                       loff_t offset, unsigned long nr_segs)
{
    ssize_t ret, rsp_len;
    struct file *file = iocb->ki_filp;
    char *kkv_req_buf=file->private_data;

#ifdef DEBUG_KKV_FS
    printk("the file name: %s\n", file->f_dentry->d_name.name);
#endif

    if (nr_segs != 1)
        return -1;

    //TODO: handle the scatter-gatter buffer
    __copy_from_user(kkv_req_buf, iov[0].iov_base, iov[0].iov_len);

    ret=kkv_process_req(kkv_req_buf,KKV_REQ_BUF_SIZE,&rsp_len);
    //We have a hack here:
    //won't copy the response packet back to user if it contains no payload.
    if(ret>0) {
        __copy_to_user(iov[0].iov_base,kkv_req_buf,rsp_len);
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
