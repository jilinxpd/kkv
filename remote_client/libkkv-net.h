/*
 * Userspace library for In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */


#include <stdint.h>

#define LIBKKV_RESULT_OK 0
#define LIBKKV_RESULT_ERROR 1


void *libkkv_create(char *ip, char *port);
int libkkv_set(void *kh, char *key, uint32_t key_len, char *value, uint32_t value_len);
int libkkv_add(void *kh, char *key, uint32_t key_len, char *value, uint32_t value_len);
int libkkv_replace(void *kh, char *key, uint32_t key_len, char *value, uint32_t value_len);
int libkkv_get(void *kh, char *key, uint32_t key_len, char **value, uint32_t *value_len);
int libkkv_delete(void *kh, char *key, uint32_t key_len);
int libkkv_shrink(void *kh);
int libkkv_free(void *kh);

