/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

extern const struct address_space_operations kkv_aops;

extern const struct file_operations kkv_file_operations;

int prepare_in_file(void);
void clean_in_file(void);