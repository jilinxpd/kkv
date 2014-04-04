/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

struct inode *kkv_get_inode(struct super_block *sb,
        const struct inode *dir, umode_t mode, dev_t dev);