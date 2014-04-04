/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include "kkv.h"
#include "file.h"

static const struct inode_operations kkv_file_inode_operations = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
};

static const struct inode_operations kkv_dir_inode_operations;

static struct backing_dev_info kkv_backing_dev_info = {
	.name = "kkv",
	.ra_pages = 0,
	.capabilities = BDI_CAP_NO_ACCT_AND_WRITEBACK |
	BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
	BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};

struct inode *kkv_get_inode(struct super_block *sb,
	const struct inode *dir, umode_t mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &kkv_aops;
		inode->i_mapping->backing_dev_info = &kkv_backing_dev_info;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_size = 1 << 20;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &kkv_file_inode_operations;
			inode->i_fop = &kkv_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &kkv_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done.
 * SMP-safe.
 */
static int kkv_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = kkv_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int kkv_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = kkv_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int kkv_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return kkv_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int kkv_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = kkv_get_inode(dir->i_sb, dir, S_IFLNK | S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname) + 1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
		} else
			iput(inode);
	}
	return error;
}

static const struct inode_operations kkv_dir_inode_operations = {
	.create = kkv_create,
	.lookup = simple_lookup,
	.link = simple_link,
	.unlink = simple_unlink,
	.symlink = kkv_symlink,
	.mkdir = kkv_mkdir,
	.rmdir = simple_rmdir,
	.mknod = kkv_mknod,
	.rename = simple_rename,
};
