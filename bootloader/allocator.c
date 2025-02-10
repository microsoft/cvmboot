// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <utils/allocator.h>

static void* _allocate(size_t n)
{
    return AllocatePool(n);
}

static void _free(void* ptr)
{
    FreePool(ptr);
}

allocator_t __allocator = { _allocate, _free };
