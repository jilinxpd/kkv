/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013-2014 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/aio.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <asm/atomic.h>
#include "kkv.h"
#include "server.h"


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
    __u32 id;
    __u32 command;
    __u32 key_len;
    __u32 value_len;
    char data[0];
} kkv_packet;

struct kkv_request {
    __u32 command;
    char *key;
    ssize_t nkey;
    char *value;
    ssize_t nvalue;
};

static struct mutex engine_lock;

void init_protocol(void){

	mutex_init(&engine_lock);

}


static int kkv_process_network(void *conf, int init)
{
    int ret=0;

    if(init) {
        ret=init_server(conf);
#ifdef DEBUG_KKV_ENGINE
        printk("init_server=%d\n",ret);
#endif
    } else {
        close_server();
#ifdef DEBUG_KKV_ENGINE
        printk("close_server\n");
#endif
    }
    return ret;
}

/* only support text key/value currently.
 */
static int kkv_parse_req(char *req_buf, ssize_t max_len, struct kkv_request *req)
{
    kkv_packet *pk;

    pk=(kkv_packet*)req_buf;
    req->command=pk->command;
    req->nkey=pk->key_len;
    req->nvalue=pk->value_len ? pk->value_len : (max_len-sizeof(kkv_packet)-req->nkey);
    req->key=req_buf+sizeof(kkv_packet);
    req->value=req->key+req->nkey;

    return 0;
}

static ssize_t kkv_create_rsp(char *rsp_buf, struct kkv_request *req)
{
    ssize_t len;
    kkv_packet *pk;
    pk=(kkv_packet*)rsp_buf;
    pk->command=req->command;
    pk->key_len=req->nkey;
    pk->value_len=req->nvalue;
    len=sizeof(kkv_packet)+req->nkey+req->nvalue;

    return len;
}

/*
 * process the request packet in io_buf, and then put the response packet into io_buf.
 * @return: indicates whether the response packet contains payload or not
 */
ssize_t kkv_process_req(char *io_buf, ssize_t max_len, ssize_t *rsp_len)
{
    ssize_t ret;
    struct kkv_request req= {
        .command=0,
        .nkey=0,
        .key=0,
        .nvalue=0,
        .value=0
    };

    ret = kkv_parse_req(io_buf, max_len,&req);
    if (ret != 0) {
        return -EINVAL;
    }

#ifdef DEBUG_KKV_ENGINE
    printk("the command is: %d\n", req.command);
    printk("the nkey is: %ld\n", req.nkey);
    if(req.key)
        printk("the key is: %s\n", req.key);
    printk("the nvalue is: %ld\n", req.nvalue);
    if(req.value)
        printk("the value is: %s\n", req.value);
#endif

    ret = -EINVAL;
    switch (req.command) {
    case COMMAND_CONFIG:
        ret=kkv_process_network(req.value,1);
        break;

    case COMMAND_DECONFIG:
        ret=kkv_process_network(0,0);
        break;

    case COMMAND_GET:
		mutex_lock(&engine_lock);
        ret = engine_get(req.key, req.nkey, req.value, req.nvalue);
		mutex_unlock(&engine_lock);
        if (ret > 0) {
            req.command=COMMAND_ACK;
            req.nvalue=ret;
        } else {
            req.command=COMMAND_NACK;
            req.nvalue=0;
        }
        goto rsp;

    case COMMAND_SET:
		mutex_lock(&engine_lock);
        ret = engine_set(req.key, req.nkey, req.value, req.nvalue);
		mutex_unlock(&engine_lock);
        break;

    case COMMAND_ADD:
		mutex_lock(&engine_lock);
        ret = engine_add(req.key, req.nkey, req.value, req.nvalue);
		mutex_unlock(&engine_lock);
        break;

    case COMMAND_REPLACE:
		mutex_lock(&engine_lock);
        ret = engine_replace(req.key, req.nkey, req.value, req.nvalue);
		mutex_unlock(&engine_lock);
        break;

    case COMMAND_DELETE:
		mutex_lock(&engine_lock);
        ret = engine_delete(req.key, req.nkey);
		mutex_unlock(&engine_lock);
        break;

    case COMMAND_SHRINK:
        ret = engine_shrink();
        break;
    }

    if (ret < 0) {
        req.command=COMMAND_NACK;
    } else {
        req.command=COMMAND_ACK;
    }
    req.nkey=0;
    req.nvalue=0;

rsp:
    *rsp_len=kkv_create_rsp(io_buf,&req);

    return ret;
}
