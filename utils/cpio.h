// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_CPIO_H
#define _CVMBOOT_UTILS_CPIO_H

#include <stdint.h>
#include <stddef.h>

/* Get CPIO file, where data_out points directly into the CPIO archive */
int cpio_get_file_direct(
    const void* cpio_data,
    size_t cpio_size,
    const char* path,
    const void** data_out,
    size_t* size_out);

/* Get CPIO file, where data_out is a malloc copy of the file */
int cpio_get_file(
    const void* cpio_data,
    size_t cpio_size,
    const char* path,
    void** dest_data,
    size_t* dest_size);

#endif /* _CVMBOOT_UTILS_CPIO_H */
