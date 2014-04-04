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
	uint32_t offset; //the end of the used space in the slab.
};

#define INIT_NUM_FREE_SLAB 8
#define SLAB_SIZE (2 * PAGE_SIZE)

//we put the slab sturct at the end of its space.
#define ADDR_TO_SLAB(addr) (addr + SLAB_SIZE - sizeof(struct slab))
#define SLAB_TO_ADDR(slabp) ((void*)slabp + sizeof(struct slab) - SLAB_SIZE)

/*
 * Global list for free slabs.
 */
static struct list_head free_list;
//spinlock_t free_lock;

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

static struct slab *alloc_slab(void)
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
		new_slab = ADDR_TO_SLAB(addr);
		new_slab->offset = 0;
#ifdef DEBUG_KKV_STAT
		used_mem++;
#endif
	}
#ifdef DEBUG_KKV_SLAB
	printk("slab nr=%d, slab addr=0x%lx, slab size=%ld\n", ++nr, (u_long) new_slab, SLAB_SIZE);
#endif
	return new_slab;
}

static inline int init_free_list(void)
{
#ifdef DEBUG_KKV_SLAB
	static int nr = 0;
#endif

	int i;
	struct list_head *one;
	struct list_head *new_list;

	if ((new_list = (struct list_head *) alloc_slab()) == NULL)
		return -1;

	INIT_LIST_HEAD(new_list);

	for (i = INIT_NUM_FREE_SLAB; i > 1; i--) {
		one = (struct list_head *) alloc_slab();
		if (one == NULL)
			return -1;
		list_add_tail(one, new_list);
	}

	// spin_lock(free_lock);
	list_add_tail(&free_list, new_list);
	// spin_unlock(free_lock);
#ifdef DEBUG_KKV_SLAB
	printk("init_free_list nr=%d\n", ++nr);
#endif
	return 0;
}

static struct slab *get_free_slab(void)
{
	struct list_head *free_slab;

	if (list_empty(&free_list)) {
		if (init_free_list() < 0)
			return NULL;
	}

	// spin_lock(free_lock);
	free_slab = free_list.next;
	list_del(free_slab);
	// spin_unlock(free_lock);

	return(struct slab*) free_slab;
}

static struct slab *get_partial_slab(struct slab_bucket * bucket)
{
#ifdef DEBUG_KKV_SLAB
	static int nr = 0;
#endif
	struct list_head *one;

	//  spin_lock(&bucket->partial_lock);
	if (list_empty(&bucket->partial_list)) {
		// spin_unlock(&bucket->partial_lock);
		if ((one = (struct list_head *) get_free_slab()) == NULL)
			return NULL;

		//spin_lock(&bucket->partial_lock);
		list_add_tail(one, &bucket->partial_list);
		bucket->nxt_slab = one;
		// spin_unlock(&bucket->partial_lock);
#ifdef DEBUG_KKV_SLAB
		printk("partial_list refill nr=%d\n", ++nr);
#endif
		return(struct slab *) one;
	}
	//  spin_unlock(&bucket->partial_lock);

	// spin_lock(&bucket->ns_lock);
	one = bucket->nxt_slab;
	//   bucket->nxt_slab = one->next;
	// if (bucket->nxt_slab == &bucket->partial_list) {
	//     bucket->nxt_slab = bucket->nxt_slab->next;
	//  }
	//  spin_unlock(&bucket->ns_lock);
	return(struct slab*) one;
}

static void *get_free_item(struct slab_bucket * bucket)
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
		addr = SLAB_TO_ADDR(one);
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

void init_slab_system(void)
{
	INIT_LIST_HEAD(&free_list);
	//  spin_lock_init(free_lock);
	init_free_list();
}

void destroy_slab_system(void)
{
	if (!list_empty(&free_list)) {
		destroy_slab_list(&free_list);
	}
}

void init_slab_bucket(struct slab_bucket * bucket, size_t item_size)
{
	INIT_LIST_HEAD(&bucket->partial_list);
	INIT_LIST_HEAD(&bucket->full_list);
	INIT_LIST_HEAD(&bucket->free_items);
	//  spin_lock_init(&bucket->partial_lock);
	//  spin_lock_init(&bucket->full_lock);
	//   spin_lock_init(&bucket->fi_lock);
	//    spin_lock_init(&bucket->ns_lock);
	bucket->item_size = item_size;
	bucket->edge = ((SLAB_SIZE - sizeof(struct slab)) / item_size) * item_size;
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

void *alloc_item_space(struct slab_bucket * bucket)
{
	void *item;
	struct slab *one;

	if ((item = get_free_item(bucket)) != NULL)
		return item;

	if ((one = get_partial_slab(bucket)) == NULL)
		return NULL;

	item = SLAB_TO_ADDR(one) + one->offset;
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

void free_item_space(struct slab_bucket * bucket, void *item)
{
	//note that size_of(item) is at least 2^4, its enough to store struct list_head.
	list_add_tail((struct list_head *) item, &bucket->free_items);
}