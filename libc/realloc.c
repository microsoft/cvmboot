// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include "malloc.h"
#include "panic.h"

void* realloc(void* ptr, size_t size)
{
    if (ptr)
    {
        malloc_header_t* h = (malloc_header_t*)ptr - 1;
        void* new_ptr;

        if (h->magic != MALLOC_MAGIC)
        {
            Print(L"%a(): bad pointer: panic\n", __FUNCTION__);
            for (;;)
                ;
        }

        if (h->padding)
        {
            LIBC_PANIC;
        }

        if (!(new_ptr = malloc(size)))
        {
            LIBC_PANIC;
            return NULL;
        }

        if (size < h->size)
            memcpy(new_ptr, ptr, size);
        else
            memcpy(new_ptr, ptr, h->size);

        free(ptr);
        return new_ptr;
    }
    else
    {
        return malloc(size);
    }

    LIBC_PANIC;
    return NULL;
}
