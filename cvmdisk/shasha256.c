// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "shasha256.h"
#include <assert.h>
#include <common/strings.h>

void shasha256_init(shasha256_ctx_t* ctx)
{
    sha256_init(&ctx->ctx);
    ctx->len = 0;
    memset(ctx->buf, 0, sizeof(ctx->buf));
    sha256_compute(&ctx->zero_hash, ctx->buf, sizeof(ctx->buf));
}

void shasha256_update(
    shasha256_ctx_t* ctx,
    const void* data,
    size_t size,
    bool zeros)
{
    const size_t bufsz = sizeof(ctx->buf);
    const uint8_t* p = data;
    size_t n = size;
    sha256_t hash;

    /* if buffer is not empty (and not full) */
    if (ctx->len)
    {
        size_t rem = bufsz - ctx->len;
        size_t min = (rem < n) ? rem : n;

        assert(ctx->len != bufsz);

        memcpy(ctx->buf + ctx->len, p, min);
        p += min;
        n -= min;

        /* if buffer is not full yet */
        if (ctx->len < bufsz)
            return;

        /* buffer is full, so flush it */
        if (zeros || all_zeros(ctx->buf, bufsz))
        {
            sha256_update(&ctx->ctx, &ctx->zero_hash, sizeof(sha256_t));
        }
        else
        {
            sha256_compute(&hash, ctx->buf, bufsz);
            sha256_update(&ctx->ctx, &hash, sizeof(hash));
        }
        ctx->len = 0;
    }

    if (n)
    {
        size_t nblocks = n / bufsz;
        size_t rem = n % bufsz;

        /* for each block */
        for (size_t i = 0; i < nblocks; i++)
        {
            if (zeros || all_zeros(p, bufsz))
            {
                sha256_update(&ctx->ctx, &ctx->zero_hash, sizeof(sha256_t));
            }
            else
            {
                sha256_compute(&hash, p, bufsz);
                sha256_update(&ctx->ctx, &hash, sizeof(hash));
            }
            p += bufsz;
            n -= bufsz;
        }

        /* for any bytes left over */
        if (n)
        {
            memcpy(ctx->buf, p, rem);
            ctx->len = rem;
        }
    }
}

void shasha256_final(sha256_t* hash, shasha256_ctx_t* ctx)
{
    /* if buffer is not empty (and not full) */
    if (ctx->len)
    {
        sha256_t tmp;
        sha256_compute(&tmp, ctx->buf, ctx->len);
        sha256_update(&ctx->ctx, &tmp, sizeof(hash));
    }

    sha256_final(hash, &ctx->ctx);
}
