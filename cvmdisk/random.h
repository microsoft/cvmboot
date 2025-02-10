// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_RANDOM_H
#define _CVMBOOT_CVMDISK_RANDOM_H

#include <stddef.h>
#include <stdint.h>

int get_random_bytes(void* data, size_t size);

#endif /* _CVMBOOT_CVMDISK_RANDOM_H */
