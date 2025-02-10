// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdbool.h>
#include <openssl/sha.h>
#include "sha1.h"
#include "hexstr.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

void sha1_compute(sha1_t* hash, const void* data, size_t size)
{
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data, size);
    SHA1_Final(hash->data, &ctx);
}

void sha1_compute2(
    sha1_t* hash,
    const void* data1,
    size_t size1,
    const void* data2,
    size_t size2)
{
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data1, size1);
    SHA1_Update(&ctx, data2, size2);
    SHA1_Final(hash->data, &ctx);
}

void sha1_clear(sha1_t* hash)
{
    memset(hash, 0, sizeof(sha1_t));
}

int sha1_compare(const sha1_t* x, const sha1_t* y)
{
    return memcmp(x, y, sizeof(sha1_t));
}

/* sort an array of hashes (bubble sort) */
void sha1_sort(sha1_t* hashes, size_t num_hashes)
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
            if (memcmp(&hashes[j], &hashes[j+1], sizeof(sha1_t)) > 0)
            {
                sha1_t tmp = hashes[j];
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

void sha1_format(sha1_string_t* str, const sha1_t* hash)
{
    hexstr_format(str->buf, SHA1_STRING_SIZE, hash->data, sizeof(sha1_t));
}

int sha1_scan(const char* str, sha1_t* hash)
{
    int ret = -1;

    if (!str || !hash)
        goto done;

    if (strlen(str) != SHA1_STRING_LENGTH)
        goto done;

    if (hexstr_scan(str, hash->data, sizeof(sha1_t)) != sizeof(sha1_t))
        goto done;

    ret = 0;

done:
    return ret;
}

int __sha1_extend(const sha1_t* base, const sha1_t* hash, sha1_t* result)
{
    int ret = -1;
    struct
    {
        sha1_t left;
        sha1_t right;
    }
    pair;

    if (!base || !hash || !result)
        goto done;

    pair.left = *base;
    pair.right = *hash;
    sha1_compute(result, &pair, sizeof(pair));

    ret = 0;

done:
    return ret;
}
