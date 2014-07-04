/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013-2014 jilinxpd.
 *
 * This file is released under the GPL.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/percpu.h>
#include <asm/atomic.h>
#include "session.h"


#define POLLRD (POLLIN|POLLRDNORM|POLLPRI)
#define POLLWR (POLLOUT|POLLWRNORM|POLLPRI)

typedef struct {
    struct list_head session_list;
    size_t session_num;
    // and the k/v store...
} cpu_context;

static cpu_context *cpucxts=NULL;
static struct workqueue_struct *wq=NULL;
static struct mutex counter_lock;
static struct kmem_cache *session_store=NULL;

static int get_balancer_counter(void)
{
    static int counter=0;

    int cur_counter;
    int cpu_num=num_online_cpus();

    mutex_lock(&counter_lock);
    cur_counter=(++counter) % cpu_num;
    mutex_unlock(&counter_lock);

    return cur_counter;
}

kkv_session* create_session(void (*worker_main)(struct work_struct *), struct socket *slave_socket)
{
    int ret;
    int cpu;
    cpu_context *cxt_percpu;
    kkv_session *s=NULL;

    s=(kkv_session*)kmem_cache_alloc(session_store,GFP_KERNEL);
    if(!s) {
        goto out;
    }
    INIT_WORK(&s->work,worker_main);
    s->skt=slave_socket;
    set_bit(SESSION_STATE_RCV,&s->state);

    cpu=get_balancer_counter();
    s->cpu=cpu;
    ret=queue_work_on(cpu,wq,&s->work);
    if(!ret) {
#ifdef DEBUG_KKV_SESSION
        printk("queue_work_on() failed in start_session()\n");
#endif
    }
    cxt_percpu=per_cpu_ptr(cpucxts,cpu);
    list_add_tail(&s->list,&cxt_percpu->session_list);

out:
    return s;
}

int continue_session(kkv_session *s)
{
    int ret;
    struct socket *skt;

    skt=s->skt;
    ret=skt->ops->poll(skt->file,skt,NULL);
    if(test_bit(SESSION_STATE_RCV,&s->state)) { //in receiving
        if(!(ret & POLLRD)) { //if there's data to read
#ifdef DEBUG_KKV_SESSION
            printk("won't queue_work() in continue_session(): POLLRD\n");
#endif
            return -EAGAIN;
        }
    } else if(test_bit(SESSION_STATE_SND,&s->state)) { //in sending
        if(!(ret & POLLWR)) { //if there's space to write
#ifdef DEBUG_KKV_SESSION
            printk("won't queue_work() in continue_session(): POLLWR\n");
#endif
            return -EAGAIN;
        }
    }

#ifdef DEBUG_KKV_SESSION
    printk("target=%d, cur=%d\n",s->cpu,get_cpu());
    put_cpu();
#endif

    ret=queue_work_on(s->cpu,wq,&s->work);
    if(!ret) {
#ifdef DEBUG_KKV_SESSION
        printk("queue_work() failed in continue_session()\n");
#endif
        return -EFAULT;
    }
    return 0;
}

void destroy_session(kkv_session *s)
{
    s->skt->ops->shutdown(s->skt,SHUT_RDWR);
    sock_release(s->skt);
    //other cleanups...
    kmem_cache_free(session_store,s);
}

int init_workers(void)
{
    int ret=0;
    int cpu;
    cpu_context *cxt_percpu;

    cpucxts=alloc_percpu(cpu_context);
    if(!cpucxts) {
#ifdef DEBUG_KKV_SESSION
        printk("alloc_percpu() failed in init_workers()\n");
#endif
        ret=-ENOMEM;
        goto out0;
    }

    for_each_online_cpu(cpu) {
        cxt_percpu=per_cpu_ptr(cpucxts,cpu);
        INIT_LIST_HEAD(&cxt_percpu->session_list);
        cxt_percpu->session_num=0;
        // and the k/v store...
    }

    session_store=kmem_cache_create("kkv_session_store", sizeof(kkv_session), 0, SLAB_HWCACHE_ALIGN, NULL);
    if(!session_store) {
#ifdef DEBUG_KKV_SESSION
        printk("kmem_cache_create() failed in init_workers()");
#endif
        ret=-ENOMEM;
        goto out1;
    }

    wq=create_workqueue("kkvworker");
    if(!wq) {
#ifdef DEBUG_KKV_SESSION
        printk("create_workqueue() failed in init_workers()\n");
#endif
        ret=-ENOMEM;
        goto out2;
    }

    mutex_init(&counter_lock);
    return 0;

out2:
    kmem_cache_destroy(session_store);
out1:
    free_percpu(cpucxts);
    cpucxts=NULL;
out0:
    return ret;
}

void destroy_workers(void)
{
    int cpu;
    cpu_context *cxt_percpu;
    struct list_head *ptr,*tmp,*head;
    kkv_session *s;

    destroy_workqueue(wq);

    //destroy sessions
    for_each_online_cpu(cpu) {
#ifdef DEBUG_KKV_SESSION
        printk("destroy sessions on cpu %d\n",cpu);
#endif
        cxt_percpu=per_cpu_ptr(cpucxts,cpu);
        head=&cxt_percpu->session_list;
        list_for_each_safe(ptr,tmp,head) {
            s=(kkv_session*)ptr;
#ifdef DEBUG_KKV_SESSION
            printk("session=%p\n",s);
#endif
            destroy_session(s);
        }
    }
    free_percpu(cpucxts);

    kmem_cache_destroy(session_store);

    // destroy the k/v store...
}
