/*
 * Userspace Client for In-Kernel Key/Value Store.
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

#include "libkkv-net.h"


void print_usage()
{
    printf("usage:\n"\
           "\t kkv-net {ip} {port} {operation}\n"\
           "\t operation:\n"\
           "\t\t get {key}\n"\
           "\t\t set {key} {value}\n"\
           "\t\t add {key} {value}\n"\
           "\t\t replace {key} {value}\n"\
           "\t\t delete {key}\n"\
           "\t\t shrink\n"\
           "\n"\
          );
}

int main(int argc, char *argv[])
{
    char *op;
    char *ip,*port;
    void *kh;
    int ret;
    uint32_t key_len=0;
    uint32_t value_len=0;
    char *key=NULL;
    char *value=NULL;

    if(argc<4) {
        print_usage();
        return -1;
    }

    ip=argv[1];
    port=argv[2];
    op=argv[3];
    if(argc>4) {
        key=argv[4];
        key_len=strlen(key)+1;
    }
    if(argc>5) {
        value=argv[5];
        value_len=strlen(value)+1;
    }
    kh=libkkv_create(ip,port);
    if(!kh) {
        printf("libkkv_create() failed\n");
        return -2;
    }

    if(!strcmp(op,"get")) {
        ret=libkkv_get(kh,key,key_len,&value,&value_len);
    } else if(!strcmp(op,"set")) {
        ret=libkkv_set(kh,key,key_len,value,value_len);
    } else if(!strcmp(op,"add")) {
        ret=libkkv_add(kh,key,key_len,value,value_len);
    } else if(!strcmp(op,"replace")) {
        ret=libkkv_replace(kh,key,key_len,value,value_len);
    } else if(!strcmp(op,"delete")) {
        ret=libkkv_delete(kh,key,key_len);
    } else if(!strcmp(op,"shrink")) {
        ret=libkkv_shrink(kh);
    } else {
        print_usage();
        ret=-1;
    }

    if(ret!=LIBKKV_RESULT_OK) {
        printf("Failed operation=%s, ret=%d\n",op,ret);
        goto exit;
    }
    printf("operation=%s, key_len=%u, value_len=%u, key=%s, value=%s\n",op,key_len,value_len,key,value);

exit:
    libkkv_free(kh);
    return ret;
}
