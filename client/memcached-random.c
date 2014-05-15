/*
 * Random test for memcached.
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
#include <sys/types.h>
#include <sys/stat.h>

#include <libmemcached-1.0/memcached.h>

#define BUF_SIZE 1024
#define PAYLOAD_STEP 16
#define PAYLOAD_MINLEN 64
#define PAYLOAD_MAXLEN (BUF_SIZE-PAYLOAD_MINLEN)

#ifdef VERBOSE_MEMCACHED_CLIENT
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif


static inline void compute_len(int i, int nr, size_t *key_len, size_t *value_len)
{
    static size_t payload_len=PAYLOAD_MAXLEN;
    static char up=0;

    size_t len;

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

static inline void generate_key_value(int i, char *buf, char seed, char **key, size_t key_len, char **value, size_t value_len)
{
    char *key_buf, *value_buf;
    int j,t;

    key_buf=buf;
    value_buf=buf+key_len+1;

    if(key) {
        for(j=0,t=i; j<key_len; j++) {
            key_buf[j]=(t&7)+seed;
            t>>=3;
        }
        key_buf[key_len]='\0';
        *key=key_buf;
    }

    if(value) {
        memset(value_buf,'a',value_len);
        value_buf[value_len]='\0';
        *value=value_buf;
    }
}


static void random_get(int nr, char *buf, char seed, memcached_st *memc)
{
    int i;
    uint32_t flags;
    memcached_return_t rc;
    size_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,NULL);
        generate_key_value(i,buf,seed,&key,key_len,NULL,0);

        value=memcached_get(memc,key,key_len,&value_len,&flags,&rc);
        if(memcached_failed(rc)) {
            printf("memcached_get() failed: %s\n",memcached_strerror(memc,rc));
        }
        PRINTF("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
        if(value)
            free(value);
    }
}

static void random_set(int nr, char *buf, char seed, memcached_st *memc)
{
    int i;
    memcached_return_t rc;
    size_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,&value_len);
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        rc=memcached_set(memc,key,key_len,value,value_len,(time_t)0,(uint32_t)0);
        if(memcached_failed(rc)) {
            printf("memcached_set() failed: %s\n",memcached_strerror(memc,rc));
        }
        PRINTF("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
    }
}

static void random_add(int nr, char *buf, char seed, memcached_st *memc)
{
    int i;
    memcached_return_t rc;
    size_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,&value_len);
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        rc=memcached_add(memc,key,key_len,value,value_len,(time_t)0,(uint32_t)0);
        if(memcached_failed(rc)) {
            printf("memcached_add() failed: %s\n",memcached_strerror(memc,rc));
        }
        PRINTF("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
    }
}

static void random_replace(int nr, char *buf, char seed, memcached_st *memc)
{
    int i;
    memcached_return_t rc;
    size_t key_len,value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,&value_len);
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        rc=memcached_replace(memc,key,key_len,value,value_len,(time_t)0,(uint32_t)0);
        if(memcached_failed(rc)) {
            printf("memcached_replace() failed: %s\n",memcached_strerror(memc,rc));
        }
        PRINTF("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
    }
}

static void random_delete(int nr, char *buf, char seed, memcached_st *memc)
{
    int i;
    memcached_return_t rc;
    size_t key_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        compute_len(i,nr,&key_len,NULL);
        generate_key_value(i,buf,seed,&key,key_len,NULL,0);

        rc=memcached_delete(memc,key,key_len,(time_t)0);
        if(memcached_failed(rc)) {
            printf("memcached_delete() failed: %s\n",memcached_strerror(memc,rc));
        }
        PRINTF("i=%d, key_len=%ld\n",i,key_len);
    }
}

void print_usage()
{
    printf("\nusage:\n"
           "memcached-random {options} {server}\n"
           "\t-o {s|a|r|d|g} the operation, one of set, add, replace, delete, get.\n"
           "\t-n the # of k/v pairs.\n"
           "\t-k the seed of key.\n\n"
          );
}

int main(int argc, char *argv[])
{
    int i;
	int nr;
	int ret=0;
    char op;
    char seed='0';
    char *server_addr;//ip addr or absolute path to unix socket file
    char buf[BUF_SIZE];

    memcached_st *memc;
    memcached_server_st *servers;
    memcached_return_t rc;

    if(argc<5) {
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
    server_addr=argv[argc-1];

    memc=memcached_create(NULL);
    servers=memcached_server_list_append_with_weight(NULL,server_addr,0,0,&rc);
    memcached_server_push(memc,servers);
    memcached_server_list_free(servers);

    switch(op) {
    case 's':
        random_set(nr,buf,seed,memc);
        break;
    case 'a':
        random_add(nr,buf,seed,memc);
        break;
    case 'r':
        random_replace(nr,buf,seed,memc);
        break;
    case 'd':
        random_delete(nr,buf,seed,memc);
        break;
    case 'g':
        random_get(nr,buf,seed,memc);
        break;
    default:
        print_usage();
        ret= -1;
    }

    memcached_free(memc);
    return ret;
}
