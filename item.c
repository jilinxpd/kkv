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

/*
 * The power ranges from 4,5,6,...,19.
 */
#define MAX_BUCKET_POWER 16

//TODO: we'd better use a link list, instead of an array.
//because the array is limited.
#define MAX_SHRINK_ITEMS 256
static struct item * free_items[MAX_SHRINK_ITEMS];
static int nr_free_items = 0;


struct slab_bucket {
    struct kmem_cache *items;
    ssize_t count;
};

static struct slab_bucket *buckets;

static inline int getPower(ssize_t size) {
    int power = 0;
    size_t i = size;
    while (i > 0) {
        power++;
        i >>= 1;
    }
    if (size == 1 << (power - 1)) return power - 1;
    else return power;
}

static struct item *alloc_item(ssize_t size) {

    struct item *it;
    int idx;
    char name[8];

    idx = getPower(size) - 4;

    if (idx > MAX_BUCKET_POWER) {
        // printk("item size is too much.\n");
        return NULL;
    }
    if (!buckets[idx].items) {
        sprintf(name, "kkv%d", idx);
        buckets[idx].items = kmem_cache_create(name, 1 << (idx + 4), 0, SLAB_HWCACHE_ALIGN, NULL);
    }
    it = kmem_cache_alloc(buckets[idx].items, GFP_KERNEL);
    if (it)
        buckets[idx].count++;

    return it;
}

struct item *create_item(char *key, ssize_t nkey, char *value, ssize_t nvalue) {
    struct item *it;
    ssize_t padded_nkey;
    ssize_t value_offset;
    ssize_t size;

    padded_nkey = PADDED_KEY_SIZE(nkey);
    value_offset = sizeof (struct item) +padded_nkey;
    size = value_offset + nvalue;

    it = alloc_item(size);
    if (!it)
        return NULL;

    memcpy(it->data, key, nkey);
    memcpy(it->data + padded_nkey, value, nvalue);

    it->refcount = 0;
    it->value_offset = value_offset;
    it->size = size;

    return it;
}

int unlink_item(struct item *it) {
    free_items[nr_free_items++] = it;
    //TODO: we need a lock here.
    if (nr_free_items == MAX_SHRINK_ITEMS) {
        shrink_item_system();
    }
    return nr_free_items;
}

 static int free_item(struct item *it) {

    int idx;

    idx = getPower(it->size) - 4;

    buckets[idx].count--;
    kmem_cache_free(buckets[idx].items, it);
    return 0;
}

int init_item_system(void) {
    buckets = NULL;
    buckets = kcalloc(MAX_BUCKET_POWER, sizeof (struct slab_bucket), GFP_KERNEL);

    return buckets ? 0 : -1;
}

void destroy_item_system(void) {
    int i;
    for (i = 0; i < MAX_BUCKET_POWER; i++) {
        if (buckets[i].items)
            kmem_cache_destroy(buckets[i].items);
    }
    kfree(buckets);
    buckets = NULL;
}

void shrink_item_system(void) {
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
