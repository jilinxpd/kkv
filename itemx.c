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

#define HT_SIZE_1st_LEVEL 1024
#define HT_SIZE_2nd_LEVEL 1024
#define HT_SIZE_3rd_LEVEL 128

typedef void *ht_entry;

static ht_entry *ht_root;
static struct kmem_cache *itemx_store;

#ifdef DEBUG_KKV_STAT
static ssize_t used_mem = 0;
static ssize_t freed_mem = 0;

ssize_t used_memory_in_itemx_system(void)
{
	return used_mem;
}

ssize_t freed_memory_in_itemx_system(void)
{
	return freed_mem;
}
#endif

/* find the target itemx.
 * used by get, replace.
 */
struct itemx *find_itemx(uint32_t key_md, char *key, ssize_t nkey)
{
	struct itemx *cur;
	ht_entry cur_ent;
	ht_entry *cur_ht;

	//lookup in the 1st hash table.
	cur_ht = ht_root;
	cur_ent = cur_ht[(key_md >> 22)&0x3FF];
	if (!cur_ent)
		return NULL;

	//lookup in the 2nd hash table.
	cur_ht = (ht_entry*) cur_ent;
	cur_ent = cur_ht[(key_md >> 12)&0x3FF];
	if (!cur_ent)
		return NULL;

	//lookup in the 3rd hash table.
	cur_ht = (ht_entry*) cur_ent;
	cur_ent = cur_ht[(key_md & 0xFFF) % HT_SIZE_3rd_LEVEL];

	//lookup in the list.
	cur = (struct itemx*) cur_ent;
	while (cur) {
		if (cur->key_md == key_md && KEY_SIZE_OF_ITEM(cur->it) == PADDED_KEY_SIZE(nkey) && !strncmp(KEY_OF_ITEM(cur->it), key, nkey)) {
			return cur;
		}
		cur = cur->next;
	}

	return NULL;
}

/* find the target itemx, besides, find the list where the itemx exists.
 * used by add, delete.
 */
struct itemx *locate_itemx(uint32_t key_md, char *key, ssize_t nkey, struct itemx ***cur_header, int force)
{
	struct itemx *cur;
	ht_entry cur_ent;
	ht_entry *cur_ht;
	uint32_t idx;

	//lookup in the 1st hash table.
	cur_ht = ht_root;
	cur_ent = cur_ht[idx = (key_md >> 22)&0x3FF];
	if (!cur_ent) {
		if (force) {
			cur_ent = cur_ht[idx] = kzalloc(HT_SIZE_2nd_LEVEL * sizeof(ht_entry), GFP_KERNEL);
			if (!cur_ent) {
				return NULL;
			}
#ifdef DEBUG_KKV_STAT
			used_mem += HT_SIZE_2nd_LEVEL * sizeof(ht_entry);
#endif
		} else {
			return NULL;
		}
	}

	//lookup in the 2nd hash table.
	cur_ht = (ht_entry*) cur_ent;
	cur_ent = cur_ht[idx = (key_md >> 12)&0x3FF];
	if (!cur_ent) {
		if (force) {
			cur_ent = cur_ht[idx] = kzalloc(HT_SIZE_3rd_LEVEL * sizeof(ht_entry), GFP_KERNEL);
			if (!cur_ent) {
				return NULL;
			}
#ifdef DEBUG_KKV_STAT
			used_mem += HT_SIZE_3rd_LEVEL * sizeof(ht_entry);
#endif
		} else {
			return NULL;
		}
	}

	//lookup in the 3rd hash table.
	cur_ht = (ht_entry*) cur_ent;
	cur_ent = cur_ht[idx = (key_md & 0xFFF) % HT_SIZE_3rd_LEVEL];
	*cur_header = (struct itemx **) (cur_ht + idx);

	//lookup in the list.
	cur = (struct itemx*) cur_ent;
	while (cur) {
		if (cur->key_md == key_md && KEY_SIZE_OF_ITEM(cur->it) == PADDED_KEY_SIZE(nkey) && !strncmp(KEY_OF_ITEM(cur->it), key, nkey)) {
			return cur;
		}
		cur = cur->next;
	}

	return NULL;
}

struct itemx *create_itemx(uint32_t key_md, struct item *it)
{
	struct itemx *itx;
	itx = kmem_cache_alloc(itemx_store, GFP_KERNEL);
	if (itx) {
		itx->it = it;
		itx->key_md = key_md;
		it->refcount++;
#ifdef DEBUG_KKV_STAT
		used_mem += sizeof(struct itemx);
#endif
	}
	return itx;
}

int update_itemx(struct itemx *itx, struct item *it)
{
	struct item *oit;
	oit = itx->it;
	itx->it = it;
	it->refcount++;
	if (--oit->refcount == 0) {
		unlink_item(oit);
	}
	return 0;
}

int add_itemx(struct itemx *itx, struct itemx **cur_header)
{
	itx->next = *cur_header;
	itx->pre = NULL;
	if (*cur_header)
		(*cur_header)->pre = itx;
	*cur_header = itx;

	return 0;
}

int delete_itemx(struct itemx *itx, struct itemx **cur_header)
{
	struct item *it;

	if (itx->pre) {
		itx->pre->next = itx->next;
	} else {
		*cur_header = itx->next;
	}

	if (itx->next) {
		itx->next->pre = itx->pre;
	}

	it = itx->it;
	kmem_cache_free(itemx_store, itx);
#ifdef DEBUG_KKV_STAT
	freed_mem += sizeof(struct itemx);
#endif
	if (--it->refcount == 0) {
		unlink_item(it);
	}

	return 0;
}

int init_itemx_system(void)
{
	ht_root = NULL;
	itemx_store = NULL;

	ht_root = kzalloc(HT_SIZE_1st_LEVEL * sizeof(ht_entry), GFP_KERNEL);
	itemx_store = kmem_cache_create("kkv_itemx_store", sizeof(struct itemx), 0, SLAB_HWCACHE_ALIGN, NULL);
#ifdef DEBUG_KKV_STAT
	used_mem += HT_SIZE_1st_LEVEL * sizeof(ht_entry);
#endif
	return ht_root && itemx_store ? 0 : -1;
}

void destroy_itemx_system(void)
{
	int i, j, k;
	struct itemx *cur_header, *itx;
	ht_entry *cur_2nd_ht, *cur_3rd_ht;
	ht_entry cur_ent;

	for (i = 0; i < HT_SIZE_1st_LEVEL; i++) {
		cur_ent = ht_root[i];
		cur_2nd_ht = (ht_entry *) cur_ent;
		if (!cur_2nd_ht)
			continue;

		for (j = 0; j < HT_SIZE_2nd_LEVEL; j++) {
			cur_ent = cur_2nd_ht[j];
			cur_3rd_ht = (ht_entry *) cur_ent;
			if (!cur_3rd_ht)
				continue;

			for (k = 0; k < HT_SIZE_3rd_LEVEL; k++) {
				cur_ent = cur_3rd_ht[k];
				cur_header = (struct itemx *) cur_ent;
				while (cur_header) {
					itx = cur_header;
					cur_header = cur_header->next;
					kmem_cache_free(itemx_store, itx);
#ifdef DEBUG_KKV_STAT
					freed_mem += sizeof(struct itemx);
#endif
				}
			}
			kfree(cur_3rd_ht);
#ifdef DEBUG_KKV_STAT
			freed_mem += HT_SIZE_3rd_LEVEL * sizeof(ht_entry);
#endif
		}
		kfree(cur_2nd_ht);
#ifdef DEBUG_KKV_STAT
		freed_mem += HT_SIZE_2nd_LEVEL * sizeof(ht_entry);
#endif
	}
	kfree(ht_root);
#ifdef DEBUG_KKV_STAT
	freed_mem += HT_SIZE_1st_LEVEL * sizeof(ht_entry);
#endif
	ht_root = NULL;

	kmem_cache_destroy(itemx_store);
	itemx_store = NULL;
}
