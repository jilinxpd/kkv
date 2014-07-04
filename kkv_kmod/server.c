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
#include <linux/uio.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/workqueue.h>
#include <linux/percpu.h>
#include <asm/atomic.h>
#include "kkv.h"
#include "socket.h"
#include "session.h"

typedef struct {
    __s32 family;
    __s32 type;
    __s32 protocol;
    __s32 addrlen;
    __u8  addr[0];
} sock_entry_t ;//__attribute__((aligned(sizeof(int))));

typedef struct {
    struct work_struct work;
    struct socket *socket;
} kkv_server;

static struct workqueue_struct *wq=NULL;
static kkv_server *svr=NULL;

static void server_work(struct work_struct *work)
{
    struct socket *server_socket;
    struct socket *slave_socket;
    kkv_server *server;
    kkv_session *session;

    server=container_of(work,kkv_server,work);
    server_socket=server->socket;
again:
    slave_socket=accept_socket(server_socket);
	if (!slave_socket)
		return;

    session=create_session(session_work_socket,slave_socket);
    if(!session) {
#ifdef DEBUG_KKV_NETWORK
		        printk("create_session() failed\n");
#endif
        goto out;
    }

    set_slave_sk_callbacks(slave_socket,session);
    goto again;
out:
    slave_socket->ops->shutdown(slave_socket,SHUT_RDWR);
    sock_release(slave_socket);
}

static void server_sk_state_change(struct sock *sk)
{
#ifdef DEBUG_KKV_NETWORK
    printk("server_sk_state_change(), sk_state=%u\n",sk->sk_state);
#endif
}

static void server_sk_data_ready(struct sock *sk, int bytes)
{
    kkv_server *s;
#ifdef DEBUG_KKV_NETWORK
    printk("server_sk_data_ready(), sk_state=%u\n",sk->sk_state);
#endif

    if(sk->sk_state==TCP_LISTEN) {
        s=(kkv_server*)sk->sk_user_data;
        queue_work(wq,&s->work);
    }
}

static void server_sk_write_space(struct sock *sk)
{
#ifdef DEBUG_KKV_NETWORK
    printk("server_sk_write_space(), sk_state=%u",sk->sk_state);
#endif
}

static void set_server_sock_callbacks(struct socket *sock,void *data)
{
    struct sock *sk=sock->sk;

    write_lock_bh(&sk->sk_callback_lock);
    sk->sk_user_data=data;
    sk->sk_state_change=server_sk_state_change;
    sk->sk_data_ready=server_sk_data_ready;
    sk->sk_write_space=server_sk_write_space;
    write_unlock_bh(&sk->sk_callback_lock);
}

int init_server(void *conf)
{
    int ret=0;
    int flags=1;
    sock_entry_t *se;
    struct linger ling= {0,0};

    //create workqueue
    if(!wq) {
        wq=create_singlethread_workqueue("kkvserver");
        if(!wq) {
#ifdef DEBUG_KKV_NETWORK
            printk("create_workqueue() failed in server_init()\n");
#endif
            return -ENOMEM;
        }
    }

    if(!svr) {
        svr=kmalloc(sizeof(kkv_server),GFP_KERNEL);
        INIT_WORK(&svr->work,server_work);
    }

    //create socket
    se=(sock_entry_t *)conf;
    ret=sock_create_kern(se->family,se->type,se->protocol,&svr->socket);
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("sock_create_kern() failed=%d, family=%d, type=%d, protocol=%d\n",
               ret,se->family,se->type,se->protocol);
#endif
        goto out0;
    }

    set_server_sock_callbacks(svr->socket,svr);

    ret=kernel_setsockopt(svr->socket,SOL_SOCKET,SO_REUSEADDR,(char*)&flags,sizeof(flags));
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_setsockopt() failed=%d, level=%d, name=%d\n",ret,SOL_SOCKET,SO_REUSEADDR);
#endif
        goto out1;
    }

    ret=kernel_setsockopt(svr->socket,SOL_SOCKET,SO_KEEPALIVE,(char*)&flags,sizeof(flags));
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_setsockopt() failed=%d, level=%d, name=%d\n",ret,SOL_SOCKET,SO_KEEPALIVE);
#endif
        goto out1;
    }

    ret=kernel_setsockopt(svr->socket,SOL_SOCKET,SO_LINGER,(char*)&ling,sizeof(ling));
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_setsockopt() failed=%d, level=%d, name=%d\n",ret,SOL_SOCKET,SO_LINGER);
#endif
        goto out1;
    }

    ret=kernel_setsockopt(svr->socket,SOL_TCP,TCP_NODELAY,(char*)&flags,sizeof(flags));
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_setsockopt() failed=%d, level=%d, name=%d\n",ret,IPPROTO_TCP,TCP_NODELAY);
#endif
        goto out1;
    }

    ret=kernel_bind(svr->socket,(struct sockaddr*)se->addr,se->addrlen);
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_bind() failed=%d\n",ret);
#endif
        goto out1;
    }

    ret=kernel_listen(svr->socket,1024);
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_listen() failed=%d\n",ret);
#endif
        goto out1;
    }

    return 0;

out1:
    sock_release(svr->socket);

out0:
    svr->socket=NULL;

    return ret;
}

void close_server(void)
{
    if(wq) {
        destroy_workqueue(wq);
        wq=NULL;
    }

    if(svr) {
        if(svr->socket) {
            sock_release(svr->socket);
        }
        kfree(svr);
        svr=NULL;
    }
}
