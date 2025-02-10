// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_ALLOCATOR_H
#define _CVMBOOT_UTILS_ALLOCATOR_H

#include <stddef.h>

typedef struct allocator
{
    void* (*alloc)(size_t size);
    void (*free)(void* ptr);
}
allocator_t;

// Linking application must define this appropriately.
//
// For EFI applications:
//
//     #include <efi.h>
//     #include <efilib.h>
//
//     static void* _alloc(size_t n) { return AllocatePool(n); }
//     static void _free(void* ptr) { FreePool(ptr); }
//     allocator_t __allocator = { _alloc, _free };
//
// For POSIX applications:
//
//     #include <stdlib.h>
//
//     allocator_t __allocator = { malloc, free };
//
extern allocator_t __allocator;

#endif /* _CVMBOOT_UTILS_ALLOCATOR_H */
