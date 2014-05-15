/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013-2014 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_PROTOCOL_H
#define _KKV_PROTOCOL_H


ssize_t kkv_process_req(char *io_buf, ssize_t max_len, ssize_t *rsp_len);


#endif