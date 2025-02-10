// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_EFIVAR_H
#define _CVMBOOT_BOOTLOADER_EFIVAR_H

#include <efi.h>
#include <efilib.h>
#include <stddef.h>

int set_efi_var(
    const CHAR16* name,
    const EFI_GUID* guid,
    const void* value_data,
    size_t value_size);

#endif /* _CVMBOOT_BOOTLOADER_EFIVAR_H */
