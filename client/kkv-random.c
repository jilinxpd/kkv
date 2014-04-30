/*
* Rush test for KKV.
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
#define PAYLOAD_STEP 16
#define PAYLOAD_MINLEN 64
#define PAYLOAD_MAXLEN (BUF_SIZE-PAYLOAD_MINLEN)


static inline void compute_len(int i, int nr, uint32_t *key_len, uint32_t *value_len)
{
    static uint32_t payload_len=PAYLOAD_MAXLEN;
    static char up=0;

    uint32_t len;

    if(up) {
        payload_len+=PAYLOAD_STEP;
        if(payload_len>PAYLOAD_MAXLEN) {
            up=0;
        }
    } else {
        payload_len-=PAYLOAD_STEP;
        if(payload_len<=PAYLOAD_MINLEN) {
            up=1;
        }
    }

    len=i*payload_len/nr;
    if(len<8) len=8;
    else if(len>200) len=200;

    if(key_len)
        *key_len=len;

    if(value_len)
        *value_len=payload_len-len;
}

static inline void generate_key_value(int i, char *buf, char seed, char **key, uint32_t key_len, char **value, uint32_t value_len)
{
    char *key_buf, *value_buf;
    int j,t;

    key_buf=buf;
    value_buf=buf+key_len;

    if(key) {
        for(j=0,t=i; j<key_len; j++) {
            key_buf[j]=(t&7)+seed;
            t>>=3;
        }
        key_buf[key_len-1]='\0';
        *key=key_buf;
    }

    if(value) {
        memset(value_buf,'a',value_len);
        value_buf[value_len-1]='\0';
        *value=value_buf;
    }
}

static void random_get(int nr, char *buf, char seed, kkv_handler *kh)
{
    int i;
    int ret;
    uint32_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,NULL);
        generate_key_value(i,buf,seed,&key,key_len,NULL,0);

        ret=libkkv_get(kh,key,key_len,&value,&value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_get() failed: %d\n",ret);
        }
        printf("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
        free(value);
    }
}

static void random_set(int nr, char *buf, char seed, kkv_handler *kh)
{
    int i;
    int ret;
    uint32_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,&value_len);
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        ret=libkkv_set(kh,key,key_len,value,value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_set() failed: %d\n",ret);
        }
        printf("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
    }
}

static void random_add(int nr, char *buf, char seed, kkv_handler *kh)
{
    int i;
    int ret;
    uint32_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,&value_len);
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        ret=libkkv_add(kh,key,key_len,value,value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_add() failed: %d\n",ret);
        }
        printf("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
    }
}

static void random_replace(int nr, char *buf, char seed, kkv_handler *kh)
{
    int i;
    int ret;
    uint32_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,&value_len);
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        ret=libkkv_replace(kh,key,key_len,value,value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_replace() failed: %d\n",ret);
        }
        printf("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
    }
}

static void random_delete(int nr, char *buf, char seed, kkv_handler *kh)
{
    int i;
    int ret;
    uint32_t key_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,NULL);
        generate_key_value(i,buf,seed,&key,key_len,NULL,0);

        ret=libkkv_delete(kh,key,key_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_delete() failed: %d\n",ret);
        }
        printf("i=%d, key_len=%u\n",i,key_len);
    }
}

void print_usage()
{
    printf("\nusage:\n"
           "kkv-random {options} {file}\n"
           "\t-o {s|a|r|d|g} the operation, one of set, add, replace, delete, get.\n"
           "\t-n the # of k/v pairs.\n"
           "\t-k the seed of key.\n\n"
          );
}


int main(int argc, char *argv[])
{
    int i;
    int nr;
    int ret;
    char op;
    char seed='0';
    char *file_path;
    kkv_handler *kh;
    char buf[BUF_SIZE];

    if(argc<2) {
        print_usage();
        return -1;
    }

    for(i=1; i<argc-1; i+=2) {

        if(argv[i][0]!='-'||i+1>=argc-1) {
            print_usage();
            return -1;
        }

        switch(argv[i][1]) {
        case 'k':
            seed=argv[i+1][0];
            break;
        case 'n':
            nr=atoi(argv[i+1]);
            break;
        case 'o':
            op=argv[i+1][0];
        }
    }

    if(nr<=0)
        nr=102400;
    file_path=argv[argc-1];

    kh=libkkv_create(file_path);

    switch(op) {
    case 's':
        random_set(nr,buf,seed,kh);
        break;
    case 'a':
        random_add(nr,buf,seed,kh);
        break;
    case 'r':
        random_replace(nr,buf,seed,kh);
        break;
    case 'd':
        random_delete(nr,buf,seed,kh);
        break;
    case 'g':
        random_get(nr,buf,seed,kh);
        break;
    default:
        print_usage();
        return -1;
    }

    libkkv_free(kh);
    return 0;
}
