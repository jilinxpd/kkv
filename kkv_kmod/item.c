
/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/string.h>
#include <linux/slab.h>
#include "kkv.h"
#include "slab.h"

/*
 * The power ranges from 4,5,6,...,19.
 */
#define MIN_BUCKET_POWER 4
#define MAX_BUCKET_POWER 12

#define POWER_TO_IDX(power) (power - MIN_BUCKET_POWER)
#define IDX_TO_POWER(idx) (idx + MIN_BUCKET_POWER)

//TODO: we'd better use a link list, instead of an array.
//because the array is limited.
#define MAX_SHRINK_ITEMS 256
static struct item *free_items[MAX_SHRINK_ITEMS];
static int nr_free_items = 0;

static struct slab_bucket *buckets;

#ifdef DEBUG_KKV_STAT
static ssize_t used_mem = 0;
static ssize_t freed_mem = 0;
static ssize_t item_mem = 0;

ssize_t used_memory_in_slab_system(void);
ssize_t freed_memory_in_slab_system(void);

ssize_t used_memory_in_item_system(void)
{
    return used_mem + used_memory_in_slab_system();
}

ssize_t freed_memory_in_item_system(void)
{
    return freed_mem + freed_memory_in_slab_system();
}

ssize_t dirty_memory_in_slab_system(void)
{
    return item_mem;
}
#endif

static inline int getPower(ssize_t size)
{
    int power = 0;
    size_t i = size;
    while (i > 0) {
        power++;
        i >>= 1;
    }
    if (size == 1 << (power - 1)) return power - 1;
    else return power;
}

ssize_t read_item(struct item *it, char *buf, ssize_t nbuf)
{
    ssize_t len;
    ssize_t nleft;

    nleft = nbuf;

    while (it && nleft > 0) {
        len = VALUE_SIZE_OF_ITEM(it);
        if (len > nleft)
            len = nleft;
        memcpy(buf, VALUE_OF_ITEM(it), len);
        buf += len;
        nleft -= len;
        it = it->next;
    }
    return nbuf - nleft;
}

static struct item *__fill_item_list(char *data, ssize_t *ndata, struct item *it, ssize_t *nbuf)
{
    void *dst_addr;
    ssize_t cp_len;

    dst_addr = (void*) it + it->size - *nbuf;

    while (*ndata > 0) {
        cp_len = *nbuf < *ndata ? *nbuf : *ndata;
        memcpy(dst_addr, data, cp_len);
        dst_addr += cp_len;
        (*ndata) -= cp_len;
        (*nbuf) -= cp_len;
        if (*nbuf == 0) {
            it = it->next;
            if (!it) {
                break;
            }
            dst_addr = it->data;
            *nbuf = VALUE_SIZE_OF_ITEM(it);
#ifdef DEBUG_KKV_STAT
            item_mem += sizeof(struct item);
#endif
        }
    }
    return it;
}

static ssize_t fill_item_list(char *key, ssize_t nkey, char *value, ssize_t nvalue, struct item *it)
{
    ssize_t src_len, dst_len;
    ssize_t padded_nkey;

    src_len = nkey;
    dst_len = VALUE_SIZE_OF_ITEM(it);
#ifdef DEBUG_KKV_STAT
    item_mem += sizeof(struct item);
#endif
    padded_nkey = PADDED_KEY_SIZE(nkey);
    it->value_offset += padded_nkey;
    it = __fill_item_list(key, &src_len, it, &dst_len);

    //printk("1 src_len=%ld\n", src_len);

#ifdef DEBUG_KKV_STAT
    item_mem += (padded_nkey - src_len);
#endif
    if (src_len > 0)
        return src_len + nvalue;

    src_len = nvalue;
    dst_len -= (padded_nkey - nkey);
    __fill_item_list(value, &src_len, it, &dst_len);
#ifdef DEBUG_KKV_STAT
    item_mem += (nvalue - src_len);
#endif
    //printk("2 src_len=%ld\n", src_len);
    return src_len;
}

static inline void *alloc_item(ssize_t size, ssize_t *alloc_size)
{
    int idx, power;

    power = getPower(size);

    if (power < MIN_BUCKET_POWER) {
        //printk("item size too small: size=%ld\n", size);
        power = MIN_BUCKET_POWER;
    } else if (power > MAX_BUCKET_POWER) {
        //printk("item size too big: size=%ld\n", size);
        power = MAX_BUCKET_POWER;
    }

    *alloc_size = (1 << power);
    idx = POWER_TO_IDX(power);

    if (!buckets[idx].item_size) {
        init_slab_bucket(&buckets[idx], *alloc_size);
    }

    return alloc_item_space(&buckets[idx]);
}

static int free_item(struct item *it);

static void free_item_list(struct item *header)
{
    struct item *it;

    while (header) {
        it = header;
        header = header->next;
        free_item(it);
    }
}

static struct item *alloc_item_list(ssize_t size)
{
    ssize_t alloc_size = 0;
    struct item tmp, *it;

    //printk("size=%ld\n", size);

    it = &tmp;
    while (size > 0) {
        size += sizeof(struct item);
        if (!(it->next = alloc_item(size, &alloc_size))) {
            //printk("alloc_item() failed in alloc_item_list()\n");
            free_item_list(tmp.next);
            return NULL;
        }
        it = it->next;
        it->refcount = 0;
        it->value_offset = sizeof(struct item);
        it->size = alloc_size;
        size -= alloc_size;
        //printk("alloc_size=%ld\n", alloc_size);
    }
    it->size = size + alloc_size; //fix the size of last item.
    it->next = NULL;
    return tmp.next;
}

struct item *create_item(char *key, ssize_t nkey, char *value, ssize_t nvalue)
{
    struct item *it;
    ssize_t remain;

    //printk("padded_nkey=%ld, nkey=%ld, nvalue=%ld\n", PADDED_KEY_SIZE(nkey), nkey, nvalue);

    it = alloc_item_list(PADDED_KEY_SIZE(nkey) + nvalue);
    if (!it)
        return NULL;

    //printk("alloc_item_list() succeed\n");

    remain = fill_item_list(key, nkey, value, nvalue, it);
    if (remain > 0) {
        printk("Not fully stored: item=0x%lx, len=%ld\n", (ulong) it, remain);
    }

    //printk("fill_item_list() succeed\n");

    return it;
}

int unlink_item(struct item *it)
{
    free_items[nr_free_items++] = it;
    //TODO: we need a lock here.
    if (nr_free_items == MAX_SHRINK_ITEMS) {
        shrink_item_system();
    }
    return nr_free_items;
}

static int free_item(struct item *it)
{
    int idx;

    idx = POWER_TO_IDX(getPower(it->size));

    free_item_space(&buckets[idx], it);

    return 0;
}

int init_item_system(void)
{
#ifdef DEBUG_KKV_STAT
    used_mem += MAX_SHRINK_ITEMS * sizeof(struct item *);
#endif
    init_slab_system();
    buckets = NULL;
    buckets = kcalloc(MAX_BUCKET_POWER - MIN_BUCKET_POWER + 1, sizeof(struct slab_bucket), GFP_KERNEL);
#ifdef DEBUG_KKV_STAT
    used_mem += (MAX_BUCKET_POWER - MIN_BUCKET_POWER + 1) * sizeof(struct slab_bucket);
#endif
    return buckets ? 0 : -1;
}

void destroy_item_system(void)
{
    int i;
    for (i = 0; i < MAX_BUCKET_POWER - MIN_BUCKET_POWER + 1; i++) {
        if (buckets[i].item_size)
            destroy_slab_bucket(&buckets[i]);
    }
    kfree(buckets);
    buckets = NULL;
#ifdef DEBUG_KKV_STAT
    freed_mem += (MAX_BUCKET_POWER - MIN_BUCKET_POWER + 1) * sizeof(struct slab_bucket);
#endif
    destroy_slab_system();
}

void shrink_item_system(void)
{
    int i;
    //TODO: we need a lock here.
    for (i = nr_free_items - 1; i >= 0; i--) {
        if (free_items[i]) {
            free_item(free_items[i]);
            free_items[i] = NULL;
        }
    }
    nr_free_items = 0;
}

