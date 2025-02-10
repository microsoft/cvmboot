// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_STRHASHTBL_H
#define _CVMBOOT_CVMDISK_STRHASHTBL_H

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#define STR_HASH_TBL_MAX_CHAINS 4096

typedef struct str_hash_tbl_node str_hash_tbl_node_t;

typedef struct str_hash_tbl
{
    str_hash_tbl_node_t* chains[STR_HASH_TBL_MAX_CHAINS];
    size_t size;
}
str_hash_tbl_t;

void str_hash_tbl_init(str_hash_tbl_t* tbl);

int str_hash_tbl_insert(str_hash_tbl_t* tbl, const char* key, void* value);

int str_hash_tbl_find(const str_hash_tbl_t* tbl, const char* key, void** value);

int str_hash_tbl_release(str_hash_tbl_t* tbl, void (*dealloc)(void* value));

#endif /* _CVMBOOT_CVMDISK_STRHASHTBL_H */
