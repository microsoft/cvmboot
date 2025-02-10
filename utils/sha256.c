// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdbool.h>
#include <openssl/sha.h>
#include "sha256.h"
#include "hexstr.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

typedef struct sha256_ctx_impl
{
    SHA256_CTX ctx;
}
sha256_ctx_impl_t;

_Static_assert(sizeof(sha256_ctx_impl_t) <= sizeof(sha256_ctx_t));

void sha256_init(sha256_ctx_t* ctx)
{
    sha256_ctx_impl_t* impl = (sha256_ctx_impl_t*)ctx;
    SHA256_Init(&impl->ctx);
}

void sha256_update(sha256_ctx_t* ctx, const void* data, size_t size)
{
    sha256_ctx_impl_t* impl = (sha256_ctx_impl_t*)ctx;
    SHA256_Update(&impl->ctx, data, size);
}

void sha256_final(sha256_t* hash, sha256_ctx_t* ctx)
{
    sha256_ctx_impl_t* impl = (sha256_ctx_impl_t*)ctx;
    SHA256_Final(hash->data, &impl->ctx);
}

void sha256_compute(sha256_t* hash, const void* data, size_t size)
{
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, size);
    SHA256_Final(hash->data, &ctx);
}

void sha256_compute2(
    sha256_t* hash,
    const void* data1,
    size_t size1,
    const void* data2,
    size_t size2)
{
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data1, size1);
    SHA256_Update(&ctx, data2, size2);
    SHA256_Final(hash->data, &ctx);
}

void sha256_clear(sha256_t* hash)
{
    memset(hash, 0, sizeof(sha256_t));
}

int sha256_compare(const sha256_t* x, const sha256_t* y)
{
    return memcmp(x, y, sizeof(sha256_t));
}

/* sort an array of hashes (bubble sort) */
void sha256_sort(sha256_t* hashes, size_t num_hashes)
{
    size_t i;
    size_t j;
    size_t n;

    n = num_hashes - 1;

    for (i = 0; i < num_hashes - 1; i++)
    {
        bool swapped = false;

        for (j = 0; j < n; j++)
        {
            if (memcmp(&hashes[j], &hashes[j+1], sizeof(sha256_t)) > 0)
            {
                sha256_t tmp = hashes[j];
                hashes[j] = hashes[j+1];
                hashes[j+1] = tmp;
                swapped = true;
            }
        }

        if (!swapped)
            break;

        n--;
    }
}

void sha256_format(sha256_string_t* str, const sha256_t* hash)
{
    hexstr_format(str->buf, SHA256_STRING_SIZE, hash->data, sizeof(sha256_t));
}

int sha256_scan(const char* str, sha256_t* hash)
{
    int ret = -1;

    if (!str || !hash)
        goto done;

    if (strlen(str) != SHA256_STRING_LENGTH)
        goto done;

    if (hexstr_scan(str, hash->data, sizeof(sha256_t)) != sizeof(sha256_t))
        goto done;

    ret = 0;

done:
    return ret;
}

int __sha256_extend(
    const sha256_t* base,
    const sha256_t* hash,
    sha256_t* result)
{
    int ret = -1;
    struct
    {
        sha256_t left;
        sha256_t right;
    }
    pair;

    if (!base || !hash || !result)
        goto done;

    pair.left = *base;
    pair.right = *hash;
    sha256_compute(result, &pair, sizeof(pair));

    ret = 0;

done:
    return ret;
}
