// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_INVENTORY_H
#define _CVMBOOT_CVMDISK_INVENTORY_H

#include <stddef.h>
#include <stdlib.h>
#include <common/strarr.h>
#include "strhashtbl.h"

typedef struct inventory
{
    strarr_t names;
    strarr_t hashes;
    str_hash_tbl_t tbl;
}
inventory_t;

void get_inventory_snapshot(const char* disk, inventory_t* inventory);

void inventory_init(inventory_t* inventory);

void inventory_release(inventory_t* inventory);

void print_inventory_delta(const inventory_t* inv1, const inventory_t* inv2);

#endif /* _CVMBOOT_CVMDISK_INVENTORY_H */
