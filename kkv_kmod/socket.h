/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013-2014 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_SOCKET_H
#define _KKV_SOCKET_H


void set_slave_sk_callbacks(struct socket *sock,void *data);
void session_work_socket(struct work_struct *work);
struct socket *accept_socket(struct socket *server_socket);


#endif