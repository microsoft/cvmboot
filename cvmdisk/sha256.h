// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_SHA256_H
#define _CVMBOOT_CVMDISK_SHA256_H

#include <stddef.h>
#include <utils/sha256.h>

int sha256_compute_file_hash(sha256_t* hash, const char* path);

#endif /* _CVMBOOT_CVMDISK_SHA256_H */
