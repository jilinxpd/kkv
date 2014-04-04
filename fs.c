/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/string.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include "kkv.h"
#include "inode.h"

#define KKV_DEFAULT_MODE	0755

static const struct super_operations kkv_ops = {
	.statfs = simple_statfs,
	.drop_inode = generic_delete_inode,
	.show_options = generic_show_options,
};

struct kkv_mount_opts {
	umode_t mode;
};

enum {
	Opt_mode,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};

struct kkv_fs_info {
	struct kkv_mount_opts mount_opts;
};

static int kkv_parse_options(char *data, struct kkv_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	char *p;

	opts->mode = KKV_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
			/*
			 * We might like to report bad mount options here;
			 * but traditionally ramfs has ignored all mount options,
			 * and as it is used as a !CONFIG_SHMEM simple substitute
			 * for tmpfs, better continue to ignore other mount options.
			 */
		}
	}

	return 0;
}

static int kkv_fill_super(struct super_block *sb, void *data, int silent)
{
	struct kkv_fs_info *fsi;
	struct inode *inode;
	int err;

	save_mount_options(sb, data);

	fsi = kzalloc(sizeof(struct kkv_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi)
		return -ENOMEM;

	err = kkv_parse_options(data, &fsi->mount_opts);
	if (err)
		return err;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = KKV_MAGIC;
	sb->s_op = &kkv_ops;
	sb->s_time_gran = 1;

	inode = kkv_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static struct dentry *kkv_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, kkv_fill_super);
}

static void kkv_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

/*
 * kkv_fs_type will be exported.
 */
struct file_system_type kkv_fs_type = {
	.name = "kkv",
	.mount = kkv_mount,
	.kill_sb = kkv_kill_sb,
	.fs_flags = FS_USERNS_MOUNT,
};
