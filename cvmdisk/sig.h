// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_SIG_H
#define _CVMBOOT_CVMDISK_SIG_H

#include <utils/sig.h>
#include <utils/sha256.h>

int sig_create(
    const void* data,
    size_t size,
    const char* signtool_path,
    sig_t* sig);

void sig_dump(const sig_t* p);

#endif /* _CVMBOOT_CVMDISK_SIG_H */
