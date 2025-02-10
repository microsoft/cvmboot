// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>

void* calloc(size_t nmemb, size_t size)
{
    void* p;

    if (!(p = malloc(nmemb * size)))
        return NULL;

    return memset(p, 0, size);
}
