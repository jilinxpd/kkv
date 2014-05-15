/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "kkv.h"
#include "file.h"
#include "fs.h"
#include "server.h"
#include "session.h"

#ifdef DEBUG_KKV_STAT
ssize_t used_memory_in_file(void);
ssize_t freed_memory_in_file(void);
ssize_t used_memory_in_item_system(void);
ssize_t freed_memory_in_item_system(void);
ssize_t dirty_memory_in_slab_system(void);
ssize_t used_memory_in_itemx_system(void);
ssize_t freed_memory_in_itemx_system(void);

ssize_t total_used_mem(void)
{
    return used_memory_in_file() + used_memory_in_item_system() + used_memory_in_itemx_system();
}

ssize_t total_freed_mem(void)
{
    return freed_memory_in_file() + freed_memory_in_item_system() + freed_memory_in_itemx_system();
}

#endif

static int __init kkv_init(void)
{
    int ret;

    ret = init_item_system();
    if (ret < 0) {
#ifdef DEBUG_KKV_FS
        printk("init_item_system() failed in kkv_mount()\n");
#endif
        ret = -ENOMEM;
        goto out;
    }
#ifdef DEBUG_KKV_STAT
    printk("total used mem=%ld\n", total_used_mem());
#endif
    ret = init_itemx_system();
    if (ret < 0) {
#ifdef DEBUG_KKV_FS
        printk("init_itemx_system() failed in kkv_mount()\n");
#endif
        ret = -ENOMEM;
        goto out;
    }
#ifdef DEBUG_KKV_STAT
    printk("total used mem=%ld\n", total_used_mem());
#endif
    ret = register_filesystem(&kkv_fs_type);
    if (ret < 0) {
#ifdef DEBUG_KKV_FS
        printk("register_filesystem() failed in kkv_init()\n");
#endif
    }
    ret=init_workers();
    if(ret<0) {
#ifdef DEBUG_KKV_FS
        printk("init_worker() failed in kkv_init()\n");
#endif
    }
out:
    return ret;
}

static void __exit kkv_exit(void)
{
    close_server();
    destroy_workers();
#ifdef DEBUG_KKV_STAT
    printk("total used mem=%ld\n", total_used_mem());
    printk("dirty mem=%ld\n", dirty_memory_in_slab_system());
#endif
    unregister_filesystem(&kkv_fs_type);
    destroy_itemx_system();
#ifdef DEBUG_KKV_STAT
    printk("total freed mem=%ld\n", total_freed_mem());
#endif
    destroy_item_system();
#ifdef DEBUG_KKV_STAT
    printk("total freed mem=%ld\n", total_freed_mem());
#endif
}

module_init(kkv_init);
module_exit(kkv_exit);

MODULE_AUTHOR("jilinxpd");
MODULE_DESCRIPTION("kkv");
MODULE_LICENSE("GPL v2");
