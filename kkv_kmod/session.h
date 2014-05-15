/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013-2014 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_SESSION_H
#define _KKV_SESSION_H


#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include "kkv.h"

//state of session

#define SESSION_STATE_SND 1 //in sending
#define	SESSION_STATE_RCV 2//in receiving
#define SESSION_STATE_BUSY 3//in processing
#define SESSION_STATE_CLOSE 4//closed

typedef struct {
    struct list_head list;//linked into the per_cpu session list
    unsigned long state;//state of this session
    int cpu;//the cpu that the work runs on
    struct work_struct work;
    struct socket *skt;
    struct file *filp;
    char kkv_req_buffer[KKV_REQ_BUF_SIZE];
} kkv_session;

kkv_session*  create_session(void (*worker_main)(struct work_struct *), struct socket *slave_socket);
int continue_session(kkv_session *s);
void destroy_session(kkv_session *s);
int init_workers(void);
void destroy_workers(void);


#endif