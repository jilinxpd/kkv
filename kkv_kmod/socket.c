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
#include "protocol.h"

static ssize_t receive_data(struct socket *sk,void *buf, ssize_t len)
{
    struct kvec iov= {buf,len};
    struct msghdr msg= {.msg_flags=MSG_DONTWAIT|MSG_NOSIGNAL};

    return kernel_recvmsg(sk,&msg,&iov,1,len,msg.msg_flags);
}

static ssize_t send_data(struct socket *sk,void *buf,ssize_t len)
{
    struct kvec iov= {buf,len};
    struct msghdr msg= {.msg_flags=MSG_DONTWAIT|MSG_NOSIGNAL};

    return kernel_sendmsg(sk,&msg,&iov,1,len);
}

static void slave_sk_state_change(struct sock *sk)
{
#ifdef DEBUG_KKV_NETWORK
    printk("slave_sk_state_change(), sk_state=%u\n",sk->sk_state);
#endif
}

static void slave_sk_data_ready(struct sock *sk, int bytes)
{
    int ret;
    kkv_session *s;

#ifdef DEBUG_KKV_NETWORK
    printk("slave_sk_data_ready(), sk_state=%u, bytes=%d\n",sk->sk_state,bytes);
#endif

    if(sk->sk_state==TCP_ESTABLISHED) {
        s=(kkv_session*)sk->sk_user_data;
        ret=continue_session(s);
#ifdef DEBUG_KKV_NETWORK
        if(ret<0) {
            printk("continue_session() failed in slave_sk_data_ready(), ret=%d\n",ret);
        }
#endif
    }
}

static void slave_sk_write_space(struct sock *sk)
{
    int ret;
    kkv_session *s;

#ifdef DEBUG_KKV_NETWORK
    printk("slave_sk_write_space(), sk_state=%u",sk->sk_state);
#endif

    if(sk->sk_state==TCP_ESTABLISHED) {
        s=(kkv_session*)sk->sk_user_data;
        ret=continue_session(s);
#ifdef DEBUG_KKV_NETWORK
        if(ret<0) {
            printk("continue_session() failed in slave_sk_write_space(), ret=%d\n",ret);
        }
#endif
    }
}

void set_slave_sk_callbacks(struct socket *sock,void *data)
{
    struct sock *sk;
    sk=sock->sk;

    write_lock_bh(&sk->sk_callback_lock);
    sk->sk_user_data=data;
    sk->sk_state_change=slave_sk_state_change;
    sk->sk_data_ready=slave_sk_data_ready;
    sk->sk_write_space=slave_sk_write_space;
    write_unlock_bh(&sk->sk_callback_lock);
}

void session_work_socket(struct work_struct *work)
{
    int ret;
    ssize_t rsp_len;
    ssize_t len;
    kkv_session *s=container_of(work,kkv_session,work);
    struct socket *slave_socket=s->skt;
    char *kkv_req_buf=s->kkv_req_buffer;

    len=receive_data(slave_socket,kkv_req_buf,KKV_REQ_BUF_SIZE);
    if(len<=0) {
#ifdef DEBUG_KKV_NETWORK
        printk("receive_data() failed in worker_main(), len=%ld\n",len);
#endif
        goto again;
    }

    clear_bit(SESSION_STATE_RCV,&s->state);

    set_bit(SESSION_STATE_BUSY,&s->state);
    ret=kkv_process_req(kkv_req_buf,KKV_REQ_BUF_SIZE,&rsp_len);
    clear_bit(SESSION_STATE_BUSY,&s->state);
//    if(ret>0) {
    //currently we send out all of the data in one shot.
    //which is not proper for the request with multiple sub-requests, of course.
    set_bit(SESSION_STATE_SND,&s->state);
    len=send_data(slave_socket,kkv_req_buf,rsp_len);
    if(len>=rsp_len) {
        clear_bit(SESSION_STATE_SND,&s->state);
        set_bit(SESSION_STATE_RCV,&s->state);
    }
#ifdef DEBUG_KKV_NETWORK
    else {
        printk("send_data() imcomplete in worker_main()\n");
    }
#endif
//    }
again:
    if(slave_socket->sk->sk_state==TCP_ESTABLISHED) {
        ret=continue_session(s);
#ifdef DEBUG_KKV_NETWORK
        if(ret<0) {
            printk("continue_session() failed in session_work_socket(), ret=%d\n",ret);
        }
#endif
    }
}

struct socket *accept_socket(struct socket *server_socket)
{
    int ret;
    int flags;
    struct sock *sk;
    struct socket *slave_socket;

    sk=server_socket->sk;

    ret=sock_create_lite(sk->sk_family,sk->sk_type,sk->sk_protocol,&slave_socket);
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("sock_create_lite() failed=%d\n",ret);
#endif
        return NULL;
    }

    slave_socket->type=server_socket->type;
    slave_socket->ops=server_socket->ops;

    ret=server_socket->ops->accept(server_socket,slave_socket,O_NONBLOCK);
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("accept() failed=%d\n",ret);
#endif
        goto out;
    }

    //worker_socket->sk->sk_allocation=GFP_ATOMIC;

    ret=kernel_setsockopt(slave_socket,SOL_TCP,TCP_NODELAY,(char*)&flags,sizeof(flags));
    if(ret<0) {
#ifdef DEBUG_KKV_NETWORK
        printk("kernel_setsockopt() failed=%d, level=%d, name=%d\n",ret,SOL_TCP,TCP_NODELAY);
#endif
        goto out;
    }

    return slave_socket;
out:
    sock_release(slave_socket);
    return NULL;
}