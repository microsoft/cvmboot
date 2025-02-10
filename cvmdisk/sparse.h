// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_SPARSE_H
#define _CVMBOOT_CVMDISK_SPARSE_H

#include <utils/sha256.h>

int sparse_copy(const char* source, const char* dest);

int sparse_cat(const char* dest);

int sparse_shasha256(const char* path, sha256_t* hash);

#endif /* _CVMBOOT_CVMDISK_SPARSE_H */
