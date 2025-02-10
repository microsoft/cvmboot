// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_CONF_H
#define _CVMBOOT_BOOTLOADER_CONF_H

#include <limits.h>
#include <utils/err.h>
#include <utils/sha256.h>

typedef struct conf
{
    char cmdline[PATH_MAX];
    sha256_t roothash;
    char kernel[PATH_MAX];
    char initrd[PATH_MAX];
}
conf_t;

int conf_load(
    const void* cpio_data,
    size_t cpio_size,
    conf_t* conf,
    err_t* err);

#endif /* _CVMBOOT_BOOTLOADER_CONF_H */
