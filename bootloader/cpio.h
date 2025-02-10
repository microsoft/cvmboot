// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_CPIO_H
#define _CVMBOOT_BOOTLOADER_CPIO_H

#include <utils/paths.h>

/* makes copy of file from memory-resident CPIO archive */
int cpio_load_file(
    const void* cpio_data,
    size_t cpio_size,
    pathid_t id,
    void** data,
    size_t* size);

/* sets data pointer to memory-resident-file in CPIO archive */
int cpio_load_file_direct(
    const void* cpio_data,
    size_t cpio_size,
    pathid_t id,
    const void** data,
    size_t* size);

int cpio_load_file_by_name(
    const void* cpio_data,
    size_t cpio_size,
    const char* name,
    void** data,
    size_t* size);

int cpio_load_file_direct_by_name(
    const void* cpio_data,
    size_t cpio_size,
    const char* name,
    const void** data,
    size_t* size);

#endif /* _CVMBOOT_BOOTLOADER_CPIO_H */
