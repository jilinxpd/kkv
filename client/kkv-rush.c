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

#define BUF_SIZE 4096

#ifdef VERBOSE_KKV_CLIENT
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

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

static void rush_get(int nr, char *buf, char seed, uint32_t key_len, kkv_handler *kh)
{
    int i;
    int ret;
	uint32_t value_len;
    char *key, *value;

    for(i=0; i<nr; i++) {
        generate_key_value(i,buf,seed,&key,key_len,NULL,0);

        ret=libkkv_get(kh,key,key_len,&value,&value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_get() failed: %d\n",ret);
        }
        PRINTF("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
        free(value);
    }
}

static void rush_set(int nr, char *buf, char seed, uint32_t key_len, uint32_t value_len, kkv_handler *kh)
{
    int i;
    int ret;
    char *key, *value;

    for(i=0; i<nr; i++) {
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        ret=libkkv_set(kh,key,key_len,value,value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_set() failed: %d\n",ret);
        }
        PRINTF("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
    }
}

static void rush_add(int nr, char *buf, char seed, uint32_t key_len, uint32_t value_len, kkv_handler *kh)
{
    int i;
    int ret;
    char *key, *value;

    for(i=0; i<nr; i++) {
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        ret=libkkv_add(kh,key,key_len,value,value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_add() failed: %d\n",ret);
        }
        PRINTF("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
    }
}

static void rush_replace(int nr, char *buf, char seed, uint32_t key_len, uint32_t value_len, kkv_handler *kh)
{
    int i;
    int ret;
    char *key, *value;

    for(i=0; i<nr; i++) {
        generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

        ret=libkkv_replace(kh,key,key_len,value,value_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_replace() failed: %d\n",ret);
        }
        PRINTF("i=%d, key_len=%u, value_len=%u\n",i,key_len,value_len);
    }
}

static void rush_delete(int nr, char *buf, char seed, uint32_t key_len, kkv_handler *kh)
{
    int i;
    int ret;
    char *key, *value;

    for(i=0; i<nr; i++) {
        generate_key_value(i,buf,seed,&key,key_len,NULL,0);

        ret=libkkv_delete(kh,key,key_len);
        if(ret!=LIBKKV_RESULT_OK) {
            printf("libkkv_delete() failed: %d\n",ret);
        }
        PRINTF("i=%d, key_len=%u\n",i,key_len);
    }
}

void print_usage()
{
    printf("\nusage:\n"
           "kkv-rush {options} {file}\n"
           "\t-o {s|a|r|d|g} the operation, one of set, add, replace, delete, get.\n"
		   "\t-k the length of key.\n"
		   "\t-v the length of value.\n"
		   "\t-n the # of k/v pairs.\n\n"
          );
}


int main(int argc, char *argv[])
{
    int i;
    int nr;
	int ret=0;
	uint32_t key_len,value_len;
    char op;
	char seed='0';
    char *file_path;
    kkv_handler *kh;
    char buf[BUF_SIZE];

    if(argc<8) {
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
			key_len=atoi(argv[i+1]);
			break;
		case 'v':
			value_len=atoi(argv[i+1]);
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
        rush_set(nr,buf,seed,key_len,value_len,kh);
        break;
    case 'a':
        rush_add(nr,buf,seed,key_len,value_len,kh);
        break;
    case 'r':
        rush_replace(nr,buf,seed,key_len,value_len,kh);
        break;
    case 'd':
        rush_delete(nr,buf,seed,key_len,kh);
        break;
    case 'g':
        rush_get(nr,buf,seed,key_len,kh);
        break;
    default:
        print_usage();
        ret= -1;
    }

    libkkv_free(kh);
    return ret;
}
