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
#define MAX_BUCKET_POWER 13 

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

static struct item *alloc_item(ssize_t size)
{
	int idx;

	idx = getPower(size) - MIN_BUCKET_POWER;

	if (idx > MAX_BUCKET_POWER - MIN_BUCKET_POWER || idx < 0) {
		printk("item size is too big or too small.\n");
		return NULL;
	}

	if (!buckets[idx].item_size) {
		init_slab_bucket(&buckets[idx], 1 << (idx + MIN_BUCKET_POWER));
	}

	return alloc_item_space(&buckets[idx]);
}

struct item *create_item(char *key, ssize_t nkey, char *value, ssize_t nvalue)
{
	struct item *it;
	ssize_t padded_nkey;
	ssize_t value_offset;
	ssize_t size;

	padded_nkey = PADDED_KEY_SIZE(nkey);
	value_offset = sizeof(struct item) +padded_nkey;
	size = value_offset + nvalue;

	it = alloc_item(size);
	if (!it)
		return NULL;
#ifdef DEBUG_KKV_STAT
	item_mem += size;
#endif

	memcpy(it->data, key, nkey);
	memcpy(it->data + padded_nkey, value, nvalue);

	it->refcount = 0;
	it->value_offset = value_offset;
	it->size = size;

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

	idx = getPower(it->size) - 4;

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

