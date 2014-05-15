/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_SLAB_H
#define _KKV_SLAB_H


struct slab_bucket {
    struct list_head partial_list, full_list, *free_list; //three lists for slabs.
    struct list_head free_items; //deletion of the items will result in holes in the slab, we organize these holes in the free_items.
    struct list_head *nxt_slab; //next slab to use in partial_list.
    // spinlock_t partial_lock, full_lock; //locks for above list_heads respectively.
    // spinlock_t fi_lock; //lock for free_items.
    // spinlock_t ns_lock; //lock for nxt_slab.
    ssize_t item_size;
    ssize_t edge; //the edge of the space that can be used by items in this slab.
};

void init_slab_system(void);
void destroy_slab_system(void);
void init_slab_bucket(struct slab_bucket * bucket, ssize_t item_size);
void destroy_slab_bucket(struct slab_bucket * bucket);
void *alloc_item_space(struct slab_bucket * bucket);
void free_item_space(struct slab_bucket * bucket, void *item);


#endif