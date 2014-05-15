/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013-2014 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_SERVER_H
#define _KKV_SERVER_H


int init_server(void *conf);
void close_server(void);


#endif