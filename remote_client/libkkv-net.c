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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "libkkv-net.h"


#define BUF_SIZE 1024

#define COMMAND_CONFIG 0
#define COMMAND_DECONFIG 1
#define COMMAND_GET 10
#define COMMAND_SET 11
#define COMMAND_ADD 12
#define COMMAND_REPLACE 13
#define COMMAND_DELETE 14
#define COMMAND_SHRINK 15
#define COMMAND_ACK 20
#define COMMAND_NACK 21


typedef struct {
    int fd;//fd for current session
    char *buf;//buffer for current session
    int accu_id;//accumulated id for current session
} kkv_handler;

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

    ret=send(fd,buf,len,0);
    if(ret<0) {
        printf("send() failed in send_request(): errno=%d\n",errno);
        goto out;
    }
    ret=recv(fd,buf,BUF_SIZE,0);
    if(ret<0) {
        printf("recv() failed in send_request(): errno=%d\n",errno);
    }
out:
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

    if(pk->command==COMMAND_NACK)
        return -2;

    len=pk->value_len;
    if(len>0) {
        value_buf=malloc(len);
        memcpy(value_buf,pk->data+pk->key_len,len);
        if (value) {
            *value=value_buf;
        }
        if(value_len) {
            *value_len=len;
        }
    } else {
        if (value) {
            *value=NULL;
        }
        if(value_len) {
            *value_len=0;
        }
    }
    return len;
}

void *libkkv_create(char *ip, char *port)
{
    int ret;
    kkv_handler *kh;
    struct addrinfo hints;
    struct addrinfo *result;
    struct sockaddr_in server_addr;

    kh=(kkv_handler*)malloc(sizeof(kkv_handler));
    if(!kh) {
        goto exit;
    }

    kh->buf=(char*)malloc(BUF_SIZE);
    if(!kh->buf) {
        goto free_kh;
    }

    memset(&server_addr,0,sizeof(struct sockaddr_in));
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(atoi(port));
    ret=inet_aton(ip,&server_addr.sin_addr);
    if(!ret) {
        printf("inet_aton() failed in libkkv_create(): errno=%d\n",errno);
        goto free_buf;
    }

    kh->fd=socket(AF_INET,SOCK_STREAM,0);
    if(kh->fd<0) {
        printf("socket() failed in libkkv_create(): errno=%d\n",errno);
        goto free_buf;
    }

    ret=connect(kh->fd,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
    if(ret<0) {
        printf("connect() failed in libkkv_create(): errno=%d\n",errno);
        goto close_sock;
    }

    kh->accu_id=0;

    return kh;

close_sock:
    close(kh->fd);
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
    __u32 id=kh->accu_id++;

    len=create_request(kh->buf,id,command,key,key_len,value,value_len);
    ret=send_request(kh->fd,kh->buf,len);
    if(ret>=0)
        ret=parse_response(kh->buf,id,NULL,NULL);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_set(void *kh, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    return __libkkv_add(kh,key,key_len,value,value_len,COMMAND_SET);
}

int libkkv_add(void *kh, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    return __libkkv_add(kh,key,key_len,value,value_len,COMMAND_ADD);
}

int libkkv_replace(void *kh, char *key, uint32_t key_len, char *value, uint32_t value_len)
{
    return __libkkv_add(kh,key,key_len,value,value_len,COMMAND_REPLACE);
}

int libkkv_get(void *kh0, char *key, uint32_t key_len, char **value, uint32_t *value_len)
{
    uint32_t len;
    int ret;
    kkv_handler *kh=(kkv_handler *)kh0;
    __u32 id=kh->accu_id++;

    len=create_request(kh->buf,id,COMMAND_GET,key,key_len,NULL,0);
    ret=send_request(kh->fd,kh->buf,len);
    if(ret>=0)
        ret=parse_response(kh->buf,id,value,value_len);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_delete(void *kh0, char *key, uint32_t key_len)
{
    uint32_t len;
    int ret;
    kkv_handler *kh=(kkv_handler *)kh0;
    __u32 id=kh->accu_id++;

    len=create_request(kh->buf,id,COMMAND_DELETE,key,key_len,NULL,0);
    ret=send_request(kh->fd,kh->buf,len);
    if(ret>=0)
        ret=parse_response(kh->buf,id,NULL,NULL);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_shrink(void *kh0)
{
    uint32_t len;
    int ret;
    kkv_handler *kh=(kkv_handler *)kh0;
    __u32 id=kh->accu_id++;

    len=create_request(kh->buf,id,COMMAND_SHRINK,NULL,0,NULL,0);
    ret=send_request(kh->fd,kh->buf,len);
    if(ret>=0)
        ret=parse_response(kh->buf,id,NULL,NULL);
    return ret<0?LIBKKV_RESULT_ERROR:LIBKKV_RESULT_OK;
}

int libkkv_free(void *kh0)
{
    kkv_handler *kh=(kkv_handler *)kh0;

    close(kh->fd);
    free(kh->buf);
    free(kh);
    return LIBKKV_RESULT_OK;
}