// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include "malloc.h"
#include "panic.h"

void* malloc(size_t size)
{
    malloc_header_t* h;

    if (!(h = AllocatePool(sizeof(malloc_header_t) + size)))
    {
        LIBC_PANIC;
        return NULL;
    }

    h->magic = MALLOC_MAGIC;
    h->size = size;
    h->padding = 0;
    h->unused = 0;

    return h + 1;
}
