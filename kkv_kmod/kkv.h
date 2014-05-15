/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_KKV_H
#define _KKV_KKV_H


#define KKV_ON_KMALLOC

#define KKV_REQ_BUF_SIZE (2 * PAGE_SIZE)

/*
 * the struct item that support multi-region.
 */
struct item {
    ssize_t refcount; //reference count of this item.
    ssize_t value_offset; //the offset of the value part within this item struct.
    ssize_t size; //size of this region.
    void *next; //addr of next region.
    char data[]; //key+value
};

#define VALUE_OF_ITEM(it) ((void*)it + it->value_offset)
#define VALUE_SIZE_OF_ITEM(it) (it->size - it->value_offset)
#define KEY_OF_ITEM(it) (it->data)
#define KEY_SIZE_OF_ITEM(it) (it->value_offset - sizeof(struct item))
#define PADDED_KEY_SIZE(size) ((ssize_t)((size + 3) / 4) * 4)

struct itemx {
    struct itemx *pre;
    struct itemx *next;
    uint32_t key_md;
    struct item *it;
};

struct item *create_item(char *key, ssize_t nkey, char *value, ssize_t nvalue);
int unlink_item(struct item *it);
ssize_t read_item(struct item *it, char *buf, ssize_t nbuf);
int init_item_system(void);
void destroy_item_system(void);
void shrink_item_system(void);

struct itemx *find_itemx(uint32_t key_md, char *key, ssize_t nkey);
struct itemx *locate_itemx(uint32_t key_md, char *key, ssize_t nkey, struct itemx ***cur_header, int force);
struct itemx *create_itemx(uint32_t key_md, struct item *it);
int update_itemx(struct itemx *itx, struct item *it);
int add_itemx(struct itemx *itx, struct itemx **cur_header);
int delete_itemx(struct itemx *itx, struct itemx **cur_header);
int init_itemx_system(void);
void destroy_itemx_system(void);

ssize_t engine_set(char *key, ssize_t nkey, char *value, ssize_t nvalue);
ssize_t engine_add(char *key, ssize_t nkey, char *value, ssize_t nvalue);
ssize_t engine_replace(char *key, ssize_t nkey, char *value, ssize_t nvalue);
ssize_t engine_delete(char *key, ssize_t nkey);
ssize_t engine_shrink(void);
ssize_t engine_get(char *key, ssize_t nkey, char *value, ssize_t nvalue);


#endif