// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_SHASHA256_H
#define _CVMBOOT_CVMDISK_SHASHA256_H

#include <utils/sha256.h>

typedef struct shasha256_ctx
{
    sha256_ctx_t ctx;
    uint8_t buf[4096];
    sha256_t zero_hash;
    size_t len;
}
shasha256_ctx_t;

void shasha256_init(shasha256_ctx_t* ctx);

void shasha256_update(
    shasha256_ctx_t* ctx,
    const void* data,
    size_t size,
    bool zeros);

void shasha256_final(sha256_t* hash, shasha256_ctx_t* ctx);

#endif /* _CVMBOOT_CVMDISK_SHASHA256_H */
