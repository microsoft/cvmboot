// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_FIND_H
#define _CVMBOOT_CVMDISK_FIND_H

#include <stddef.h>
#include <stdlib.h>
#include <common/strarr.h>

int find(const char* dirname, strarr_t* names);

#endif /* _CVMBOOT_CVMDISK_FIND_H */
