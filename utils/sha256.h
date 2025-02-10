// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_SHA256_H
#define _CVMBOOT_UTILS_SHA256_H

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define SHA256_SIZE 32
#define SHA256_STRING_LENGTH (SHA256_SIZE * 2)
#define SHA256_STRING_SIZE (SHA256_STRING_LENGTH + 1)
#define SHA256_INITIALIZER { { 0 } }

typedef struct
{
    uint8_t data[SHA256_SIZE];
}
sha256_t;

typedef struct sha256_ctx
{
    uint64_t impl[16];
}
sha256_ctx_t;

typedef struct
{
    char buf[SHA256_STRING_SIZE];
}
sha256_string_t;

void sha256_init(sha256_ctx_t* ctx);

void sha256_update(sha256_ctx_t* ctx, const void* data, size_t size);

void sha256_final(sha256_t* hash, sha256_ctx_t* ctx);

void sha256_compute(sha256_t* hash, const void* data, size_t size);

void sha256_compute2(
    sha256_t* hash,
    const void* data1,
    size_t size1,
    const void* data2,
    size_t size2);

void sha256_clear(sha256_t* hash);

int sha256_compare(const sha256_t* x, const sha256_t* y);

void sha256_sort(sha256_t* hashes, size_t num_hashes);

void sha256_format(sha256_string_t* str, const sha256_t* hash);

int sha256_scan(const char* str, sha256_t* hash);

static __inline__ bool sha256_equal(const sha256_t* x, const sha256_t* y)
{
    return sha256_compare(x, y) == 0;
}

int __sha256_extend(
    const sha256_t* base,
    const sha256_t* hash,
    sha256_t* result);

static inline int sha256_extend(sha256_t* base, const sha256_t* hash)
{
    sha256_t result;
    int ret;

    if ((ret = __sha256_extend(base, hash, &result)) < 0)
        return ret;

    memcpy(base, &result, sizeof(sha256_t));
    return 0;
}

#endif /* _CVMBOOT_UTILS_SHA256_H */
