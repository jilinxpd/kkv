/*
 * Userspace library for In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libkkv.h"


#define BUF_SIZE 1024

#define COMMAND_CONFIG 0
#define COMMAND_DECONFIG 1
#define COMMAND_GET 10
#define COMMAND_SET 11
#define COMMAND_ADD 12
#define COMMAND_REPLACE 13
#define COMMAND_DELETE 14
#define COMMAND_SHRINK 15


typedef struct {
    uint32_t id;
    uint32_t command;
    uint32_t key_len;
    uint32_t value_len;
    char data[0];
} kkv_packet;




static int send_request(int fd, char *buf, uint32_t len)
{
    int ret;

    ret=write(fd,buf,len);
    if(ret<0) {
        printf("write() failed: errno=%d\n",errno);
    }
    return ret;
}

static uint32_t create_request(char *buf, uint32_t id, uint32_t command, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    kkv_packet *pk;

    pk=(kkv_packet*)buf;
    pk->id=id;
    pk->command=command;

    pk->key_len=key_len;
    if(key_len) {
        memcpy(pk->data,key,key_len);
    }

    pk->value_len=value_len;
    if(value_len) {
        memcpy(pk->data+key_len,value,value_len);
    }

    return sizeof(kkv_packet)+key_len+value_len;
}

static int parse_response(char *buf, uint32_t id, char **value, uint32_t *value_len)
{
    uint32_t len;
    kkv_packet *pk;
    char *value_buf;

    pk=(kkv_packet*)buf;
    if(pk->id!=id)
        return -1;

    len=pk->value_len;
    if(len>0) {
        value_buf=malloc(len);
        memcpy(value_buf,pk->data+pk->key_len,len);
        *value=value_buf;
        *value_len=len;
    } else {
        *value=NULL;
        *value_len=0;
    }
    return len;
}

kkv_handler *libkkv_create(char *kkv_path)
{
    kkv_handler *kh;
    int flags;
    mode_t mode;

    kh=(kkv_handler*)malloc(sizeof(kkv_handler));
    if(!kh) {
        goto exit;
    }

    kh->buf=(char*)malloc(BUF_SIZE);
    if(!kh->buf) {
        goto free_kh;
    }

    flags=O_RDWR|O_CREAT|O_DIRECT;
    mode=S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
    kh->fd=open(kkv_path,flags,mode);
    if(kh->fd<0) {
        printf("libkkv_create() failed: %d\n",errno);
        goto free_buf;
    }

    kh->accu_id=0;

    return kh;

free_buf:
    free(kh->buf);
free_kh:
    free(kh);
exit:
    return NULL;
}

static int __libkkv_add(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len, uint32_t command)
{
    uint32_t len;
    int ret;

    len=create_request(kh->buf,kh->accu_id++,command,key,key_len,value,value_len);
    ret=send_request(kh->fd,kh->buf,len);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_set(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    return __libkkv_add(kh,key,key_len,value,value_len,COMMAND_SET);
}

int libkkv_add(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    return __libkkv_add(kh,key,key_len,value,value_len,COMMAND_ADD);
}

int libkkv_replace(kkv_handler *kh, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    return __libkkv_add(kh,key,key_len,value,value_len,COMMAND_REPLACE);
}

int libkkv_get(kkv_handler *kh, char *key, uint32_t key_len, char **value, uint32_t *value_len)
{
    uint32_t len;
    int ret;
    __u32 id=kh->accu_id++;

    len=create_request(kh->buf,id,COMMAND_GET,key,key_len,NULL,0);
    ret=send_request(kh->fd,kh->buf,len);
    if(ret>=0)
        ret=parse_response(kh->buf,id,value,value_len);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_delete(kkv_handler *kh, char *key, uint32_t key_len)
{
    uint32_t len;
    int ret;

    len=create_request(kh->buf,kh->accu_id++,COMMAND_DELETE,key,key_len,NULL,0);
    ret=send_request(kh->fd,kh->buf,len);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_shrink(kkv_handler *kh)
{
    uint32_t len;
    int ret;

    len=create_request(kh->buf,kh->accu_id++,COMMAND_SHRINK,NULL,0,NULL,0);
    ret=send_request(kh->fd,kh->buf,len);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_free(kkv_handler *kh)
{
    close(kh->fd);
    free(kh->buf);
    free(kh);
    return LIBKKV_RESULT_OK;
}