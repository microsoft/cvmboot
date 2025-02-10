// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_SHA1_H
#define _CVMBOOT_UTILS_SHA1_H

#include <string.h>
#include <stdbool.h>

#define SHA1_SIZE 20
#define SHA1_STRING_LENGTH (SHA1_SIZE * 2)
#define SHA1_STRING_SIZE (SHA1_STRING_LENGTH + 1)
#define SHA1_INITIALIZER { { 0 } }

typedef struct
{
    uint8_t data[SHA1_SIZE];
}
sha1_t;

typedef struct
{
    char buf[SHA1_STRING_SIZE];
}
sha1_string_t;

void sha1_compute(sha1_t* hash, const void* data, size_t size);

void sha1_compute2(
    sha1_t* hash,
    const void* data1,
    size_t size1,
    const void* data2,
    size_t size2);

void sha1_clear(sha1_t* hash);

int sha1_compare(const sha1_t* x, const sha1_t* y);

void sha1_sort(sha1_t* hashes, size_t num_hashes);

void sha1_format(sha1_string_t* str, const sha1_t* hash);

int sha1_scan(const char* str, sha1_t* hash);

static __inline__ bool sha1_equal(const sha1_t* x, const sha1_t* y)
{
    return sha1_compare(x, y) == 0;
}

int __sha1_extend(
    const sha1_t* base,
    const sha1_t* hash,
    sha1_t* result);

static inline int sha1_extend(sha1_t* base, const sha1_t* hash)
{
    sha1_t result;
    int ret;

    if ((ret = __sha1_extend(base, hash, &result)) < 0)
        return ret;

    memcpy(base, &result, sizeof(sha1_t));
    return 0;
}

#endif /* _CVMBOOT_UTILS_SHA1_H */
