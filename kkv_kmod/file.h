/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

#ifndef _KKV_FILE_H
#define _KKV_FILE_H


extern const struct address_space_operations kkv_aops;

extern const struct file_operations kkv_file_operations;


#endif