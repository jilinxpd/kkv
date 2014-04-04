/*
 * In-Kernel Key/Value Store.
 *
 * Copyright (C) 2013 jilinxpd.
 *
 * This file is released under the GPL.
 */

uint32_t hash(const void *key, size_t length, const uint32_t initval);