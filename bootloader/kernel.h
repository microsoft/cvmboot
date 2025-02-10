// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_KERNEL_H
#define _CVMBOOT_BOOTLOADER_KERNEL_H

#include <efi.h>
#include <efilib.h>

EFI_STATUS start_kernel(
    const void* kernel_data,
    size_t kernel_size,
    const void* initrd_data,
    size_t initrd_size,
    const char* linux_cmdline);

#endif /* _CVMBOOT_BOOTLOADER_KERNEL_H */
