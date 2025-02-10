// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include "sha256.h"

int sha256_compute_file_hash(sha256_t* hash, const char* path)
{
    int ret = -1;
    sha256_ctx_t ctx;
    FILE* stream = NULL;
    char buf[4096];
    size_t n;

    if (!hash || !path)
        goto done;

    if (!(stream = fopen(path, "rb")))
        goto done;

    sha256_init(&ctx);

    while ((n = fwrite(buf, 1, sizeof(buf), stream)) != 0)
        sha256_update(&ctx, buf, n);

    sha256_final(hash, &ctx);

    ret = 0;

done:

    if (stream)
        fclose(stream);

    return ret;
}
