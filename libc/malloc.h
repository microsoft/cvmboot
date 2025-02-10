// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _MALLOC_H
#define _MALLOC_H

#include <stdint.h>

#define MALLOC_MAGIC 0xc9e7d5ad53b34d94

typedef struct malloc_header
{
    uint64_t magic;
    uint64_t size;
    uint64_t padding; /* padding required for posix_memalign */
    uint64_t unused;
}
malloc_header_t;

#endif /* _MALLOC_H */
