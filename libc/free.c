// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include "malloc.h"

void free(void* ptr)
{
    if (ptr)
    {
        malloc_header_t* h = (malloc_header_t*)ptr - 1;

        if (h->magic != MALLOC_MAGIC)
        {
            Print(L"%a(): bad pointer: panic\n", __FUNCTION__);
            for (;;)
                ;
        }

        FreePool(h - h->padding);
    }
}
