/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include "kkv.h"
#include "hash.h"

ssize_t engine_set(char *key, ssize_t nkey, char *value, ssize_t nvalue)
{
    struct item *it;
    struct itemx *itx;
    uint32_t key_md;
    struct itemx **cur_header;

    key_md = hash(key, nkey, 0);
#ifdef DEBUG_KKV_ENGINE
    printk("the key_md is 0x%x\n", key_md);
#endif
    it = create_item(key, nkey, value, nvalue);
    if (!it) {
#ifdef DEBUG_KKV_ENGINE
        printk("create_item() failed in engine_update()\n");
#endif
        return -ENOSPC;
    }

    itx = locate_itemx(key_md, key, nkey, &cur_header, 1);
    if (itx) {
        return update_itemx(itx, it);
    } else {
        itx = create_itemx(key_md, it);
        if (!itx) {
#ifdef DEBUG_KKV_ENGINE
            printk("create_itemx() failed in engine_create()\n");
#endif
            return -ENOSPC;
        }
        return add_itemx(itx, cur_header);
    }
}

ssize_t engine_add(char *key, ssize_t nkey, char *value, ssize_t nvalue)
{
    struct item *it;
    struct itemx *itx;
    uint32_t key_md;
    struct itemx **cur_header = NULL;

    key_md = hash(key, nkey, 0);
#ifdef DEBUG_KKV_ENGINE
    printk("the key_md is 0x%x\n", key_md);
#endif

    itx = locate_itemx(key_md, key, nkey, &cur_header, 1);
    if (itx) {
#ifdef DEBUG_KKV_ENGINE
        printk("locate_itemx() failed in engine_create()\n");
#endif
        return -EEXIST;
    } else if (!cur_header) {
#ifdef DEBUG_KKV_ENGINE
        printk("locate_itemx() failed in engine_update()\n");
#endif
        return -ENOSPC;
    }
    it = create_item(key, nkey, value, nvalue);
    if (!it) {
#ifdef DEBUG_KKV_ENGINE
        printk("create_item() failed in engine_update()\n");
#endif
        return -ENOSPC;
    }
    itx = create_itemx(key_md, it);
    if (!itx) {
#ifdef DEBUG_KKV_ENGINE
        printk("create_itemx() failed in engine_create()\n");
#endif
        return -ENOSPC;
    }
#ifdef DEBUG_KKV_ENGINE
    printk("itx=0x%lx, cur_header=0x%lx\n", (ulong) itx, (ulong) cur_header);
#endif
    return add_itemx(itx, cur_header);
}

ssize_t engine_replace(char *key, ssize_t nkey, char *value, ssize_t nvalue)
{
    struct item *it;
    struct itemx *itx;
    uint32_t key_md;

    key_md = hash(key, nkey, 0);
#ifdef DEBUG_KKV_ENGINE
    printk("the key_md is 0x%x\n", key_md);
#endif

    itx = find_itemx(key_md, key, nkey);
    if (!itx) {
#ifdef DEBUG_KKV_ENGINE
        printk("find_itemx() failed in engine_create()\n");
#endif
        return -ENOENT;
    }
    it = create_item(key, nkey, value, nvalue);
    if (!it) {
#ifdef DEBUG_KKV_ENGINE
        printk("create_item() failed in engine_update()\n");
#endif
        return -ENOSPC;
    }

    return update_itemx(itx, it);
}

ssize_t engine_delete(char *key, ssize_t nkey)
{
    struct itemx *itx;
    uint32_t key_md;
    struct itemx **cur_header;

    key_md = hash(key, nkey, 0);
#ifdef DEBUG_KKV_ENGINE
    printk("the key_md is 0x%x\n", key_md);
#endif

    itx = locate_itemx(key_md, key, nkey, &cur_header, 0);
    if (!itx) {
#ifdef DEBUG_KKV_ENGINE
        printk("locate_itemx() failed in engine_create()\n");
#endif
        return -ENOENT;
    }

    return delete_itemx(itx, cur_header);
}

ssize_t engine_shrink()
{
    shrink_item_system();
    return 0;
}


ssize_t engine_get(char *key, ssize_t nkey, char *value, ssize_t nvalue)
{
    struct itemx *itx;
    uint32_t key_md;

    key_md = hash(key, nkey, 0);
#ifdef DEBUG_KKV_ENGINE
    printk("the key_md is 0x%x\n", key_md);
#endif

    itx = find_itemx(key_md, key, nkey);
    if (!itx) {
#ifdef DEBUG_KKV_ENGINE
        printk("find_itemx() failed in engine_create()\n");
#endif
        return -ENOENT;
    }

    return read_item(itx->it, value, nvalue);
}
