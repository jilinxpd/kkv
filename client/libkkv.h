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


typedef struct {
    int fd;//fd for current session
    char *buf;//buffer for current session
    int accu_id;//accumulated id for current session
} kkv_handler;


kkv_handler *libkkv_create(char *kkv_path);
int libkkv_set(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len);
int libkkv_add(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len);
int libkkv_replace(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len);
int libkkv_get(kkv_handler *kh, char *key, uint32_t key_len, char **value, uint32_t *value_len);
int libkkv_delete(kkv_handler *kh, char *key, uint32_t key_len);
int libkkv_shrink(kkv_handler *kh);
int libkkv_free(kkv_handler *kh);
int libkkv_config(kkv_handler *kh, char *ip, char *port);
int libkkv_deconfig(kkv_handler *kh);

