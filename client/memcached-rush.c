/*
 * Rush test for memcached.
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

#define BUF_SIZE 4096

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


static void rush_get(int nr, char *buf, char seed, uint32_t key_len, memcached_st *memc)
{
	int i;
	uint32_t flags;
	memcached_return_t rc;
	size_t value_len;
	char *key, *value;

	for(i=0; i<nr; i++) {
		generate_key_value(i,buf,seed,&key,key_len,NULL,0);

		value=memcached_get(memc,key,key_len,&value_len,&flags,&rc);
		if(memcached_failed(rc)) {
			printf("memcached_get() failed: %s\n",memcached_strerror(memc,rc));
		}
		printf("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
		if(value)
			free(value);
	}
}

static void rush_set(int nr, char *buf, char seed, uint32_t key_len, uint32_t value_len, memcached_st *memc)
{
	int i;
	memcached_return_t rc;
	char *key, *value;

	for(i=0; i<nr; i++) {
		generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

		rc=memcached_set(memc,key,key_len,value,value_len,(time_t)0,(uint32_t)0);
		if(memcached_failed(rc)) {
			printf("memcached_set() failed: %s\n",memcached_strerror(memc,rc));
		}
		printf("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
	}
}

static void rush_add(int nr, char *buf, char seed, uint32_t key_len, uint32_t value_len, memcached_st *memc)
{
	int i;
	memcached_return_t rc;
	char *key, *value;

	for(i=0; i<nr; i++) {
		generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

		rc=memcached_add(memc,key,key_len,value,value_len,(time_t)0,(uint32_t)0);
		if(memcached_failed(rc)) {
			printf("memcached_add() failed: %s\n",memcached_strerror(memc,rc));
		}
		printf("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
	}
}

static void rush_replace(int nr, char *buf, char seed, uint32_t key_len, uint32_t value_len, memcached_st *memc)
{
	int i;
	memcached_return_t rc;
	char *key, *value;

	for(i=0; i<nr; i++) {
		generate_key_value(i,buf,seed,&key,key_len,&value,value_len);

		rc=memcached_replace(memc,key,key_len,value,value_len,(time_t)0,(uint32_t)0);
		if(memcached_failed(rc)) {
			printf("memcached_replace() failed: %s\n",memcached_strerror(memc,rc));
		}
		printf("i=%d, key_len=%ld, value_len=%ld\n",i,key_len,value_len);
	}
}

static void rush_delete(int nr, char *buf, char seed, uint32_t key_len, memcached_st *memc)
{
	int i;
	memcached_return_t rc;
	char *key, *value;

	for(i=0; i<nr; i++) {
		generate_key_value(i,buf,seed,&key,key_len,NULL,0);

		rc=memcached_delete(memc,key,key_len,(time_t)0);
		if(memcached_failed(rc)) {
			printf("memcached_delete() failed: %s\n",memcached_strerror(memc,rc));
		}
		printf("i=%d, key_len=%ld\n",i,key_len);
	}
}


void print_usage()
{
    printf("\nusage:\n"
           "memcached-rush [options] {server}\n"
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
	char op;
	char seed='0';
	char *server_addr;
	char buf[BUF_SIZE];
	uint32_t key_len,value_len;

    memcached_st *memc;
    memcached_server_st *servers;
    memcached_return_t rc;

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
    server_addr=argv[argc-1];

    memc=memcached_create(NULL);
    servers=memcached_server_list_append_with_weight(NULL,server_addr,0,0,&rc);
    memcached_server_push(memc,servers);
    memcached_server_list_free(servers);

	switch(op) {
	case 's':
		rush_set(nr,buf,seed,key_len,value_len,memc);
		break;
	case 'a':
		rush_add(nr,buf,seed,key_len,value_len,memc);
		break;
	case 'r':
		rush_replace(nr,buf,seed,key_len,value_len,memc);
		break;
	case 'd':
		rush_delete(nr,buf,seed,key_len,memc);
		break;
	case 'g':
		rush_get(nr,buf,seed,key_len,memc);
		break;
	default:
		print_usage();
		return -1;
	}

    memcached_free(memc);
    return 0;
}
