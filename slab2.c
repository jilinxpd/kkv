/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "kkv.h"
#include "slab.h"

struct slab {
	struct list_head list; //either from free_list, partial_list or full_list.
	//TODO: we need a lock here, to protect concurrent access to this slab.
	void *start_addr; //the start of the mem space.
	uint32_t offset; //the end of the used space in the slab.
};

#define INIT_NUM_FREE_SLAB 8
#define SLAB_SIZE (PAGE_SIZE << 8)

/*
 * A special slab_bucket, used to store the struct slab in a centralized place.
 */
static struct slab_bucket slab_headers;

/*
 * Global list for free slabs.
 */
static struct list_head global_free_list;
static struct list_head global_free_list_for_slab_headers;

#ifdef DEBUG_KKV_STAT
static ssize_t used_mem = 0;
static ssize_t freed_mem = 0;

ssize_t used_memory_in_slab_system(void)
{
	return used_mem * SLAB_SIZE;
}

ssize_t freed_memory_in_slab_system(void)
{
	return freed_mem * SLAB_SIZE;
}
#endif

static inline void *alloc_slab_header(void);

static struct slab *alloc_slab(int is_slabh)
{
#ifdef DEBUG_KKV_SLAB
	static int nr = 0;
#endif

	void *addr = NULL;
	struct slab *new_slab = NULL;

#ifdef KKV_ON_KMALLOC
	addr = kmalloc(SLAB_SIZE, GFP_KERNEL);
#else
	addr = vmalloc(SLAB_SIZE);
#endif	
	if (addr != NULL) {
		if (is_slabh) {
			new_slab = addr;
			new_slab->start_addr = addr;
			new_slab->offset = sizeof(struct slab);
		} else {
			new_slab = alloc_slab_header();
			new_slab->start_addr = addr;
			new_slab->offset = 0;
		}
#ifdef DEBUG_KKV_STAT
		used_mem++;
#endif
	}
#ifdef DEBUG_KKV_SLAB
	printk("slab nr=%d, slab addr=0x%lx, slab size=%ld\n", ++nr, (u_long) new_slab, SLAB_SIZE);
#endif
	return new_slab;
}

static int init_free_list(struct list_head *free_list, int is_slabh)
{
#ifdef DEBUG_KKV_SLAB
	static int nr = 0;
#endif

	int i;
	struct list_head *one;
	struct list_head *new_list;

	if ((new_list = (struct list_head *) alloc_slab(is_slabh)) == NULL)
		return -1;

	INIT_LIST_HEAD(new_list);

	for (i = INIT_NUM_FREE_SLAB; i > 1; i--) {
		one = (struct list_head *) alloc_slab(is_slabh);
		if (one == NULL)
			return -1;
		list_add_tail(one, new_list);
	}

	list_add_tail(free_list, new_list);
#ifdef DEBUG_KKV_SLAB
	printk("init_free_list nr=%d\n", ++nr);
#endif
	return 0;
}

static struct slab *get_free_slab(struct list_head *free_list, int is_slabh)
{
	struct list_head *free_slab;

	if (list_empty(free_list)) {
		if (init_free_list(free_list, is_slabh) < 0)
			return NULL;
	}

	free_slab = free_list->next;
	list_del(free_slab);

	return(struct slab*) free_slab;
}

static inline struct slab *get_partial_slab(struct slab_bucket * bucket, int is_slabh)
{
#ifdef DEBUG_KKV_SLAB
	static int nr = 0;
#endif
	struct list_head *one;

	if (list_empty(&bucket->partial_list)) {
		if ((one = (struct list_head *) get_free_slab(bucket->free_list, is_slabh)) == NULL)
			return NULL;

		list_add_tail(one, &bucket->partial_list);
		bucket->nxt_slab = one;
#ifdef DEBUG_KKV_SLAB
		printk("partial_list refill nr=%d\n", ++nr);
#endif
		return(struct slab *) one;
	}
	one = bucket->nxt_slab;
	return(struct slab*) one;
}

static inline void *get_free_item(struct slab_bucket * bucket)
{
	void *item;
	//TODO: we need require a lock for bucket->free_items
	if (list_empty(&bucket->free_items)) {
		return NULL;
	}
	item = bucket->free_items.next;
	list_del((struct list_head*) item);
	return item;
}

/*
 * list can not be empty.
 */
static void destroy_slab_list(struct list_head *header)
{
	struct list_head *one;
	void *addr;

	for (one = header->next; one != header;) {
		addr = ((struct slab*) one)->start_addr;
		one = one->next;
#ifdef DEBUG_KKV_SLAB
		printk("destroy_slab addr=0x%lx\n", (ulong) addr);
#endif
#ifdef DEBUG_KKV_STAT
		freed_mem++;
#endif
#ifdef KKV_ON_KMALLOC
		kfree(addr);
#else
		vfree(addr);
#endif
	}
}
static inline void __init_slab_bucket(struct slab_bucket * bucket, ssize_t item_size, struct list_head *flist);

void init_slab_system(void)
{
	__init_slab_bucket(&slab_headers, sizeof(struct slab), &global_free_list_for_slab_headers);
	INIT_LIST_HEAD(&global_free_list_for_slab_headers);
	init_free_list(&global_free_list_for_slab_headers, 1);

	INIT_LIST_HEAD(&global_free_list);
	init_free_list(&global_free_list, 0);
}

void destroy_slab_system(void)
{
	if (!list_empty(&global_free_list)) {
		destroy_slab_list(&global_free_list);
	}
	if (!list_empty(&global_free_list_for_slab_headers)) {
		destroy_slab_list(&global_free_list_for_slab_headers);
	}
	destroy_slab_bucket(&slab_headers);
}

static inline void __init_slab_bucket(struct slab_bucket * bucket, ssize_t item_size, struct list_head *flist)
{
	INIT_LIST_HEAD(&bucket->partial_list);
	INIT_LIST_HEAD(&bucket->full_list);
	INIT_LIST_HEAD(&bucket->free_items);
	bucket->free_list = flist;
	bucket->item_size = item_size;
	bucket->edge = (SLAB_SIZE / item_size) * item_size;
}

void init_slab_bucket(struct slab_bucket * bucket, ssize_t item_size)
{
	__init_slab_bucket(bucket, item_size, &global_free_list);
}

void destroy_slab_bucket(struct slab_bucket * bucket)
{
	if (!list_empty(&bucket->full_list)) {
		destroy_slab_list(&bucket->full_list);
	}

	if (!list_empty(&bucket->partial_list)) {
		destroy_slab_list(&bucket->partial_list);
	}
}

static inline void *__alloc_item_space(struct slab_bucket * bucket, int is_slabh)
{
	void *item;
	struct slab *one;

	if ((item = get_free_item(bucket)) != NULL)
		return item;

	if ((one = get_partial_slab(bucket, is_slabh)) == NULL)
		return NULL;

	item = one->start_addr + one->offset;
	one->offset += bucket->item_size;
	if (one->offset >= bucket->edge) {
		//the slab is full, move from partial_list to full_list.
		list_del((struct list_head *) one);
		list_add_tail((struct list_head *) one, &bucket->full_list);
	}
	//TODO: we need release the lock for current slab.
#ifdef DEBUG_KKV_SLAB
	printk("item nr=%ld, item addr=0x%lx, item size=%ld\n", one->offset / bucket->item_size, (u_long) item, bucket->item_size);
#endif
	return item;
}

static inline void *alloc_slab_header(void)
{
	return __alloc_item_space(&slab_headers, 1);
}

void *alloc_item_space(struct slab_bucket * bucket)
{
	return __alloc_item_space(bucket, 0);
}

void free_item_space(struct slab_bucket * bucket, void *item)
{
	//note that size_of(item) is at least 2^4, its enough to store struct list_head.
	list_add_tail((struct list_head *) item, &bucket->free_items);
}
