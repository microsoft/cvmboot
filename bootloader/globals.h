// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_GLOBALS_H
#define _CVMBOOT_BOOTLOADER_GLOBALS_H

#include <efi.h>
#include <efilib.h>
#include "tcg2.h"

typedef struct globals
{
    EFI_HANDLE image_handle; /* image_handle passed to efi_main() */
    EFI_SYSTEM_TABLE* system_table; /* system_table passed to efi_main() */
    EFI_TCG2_PROTOCOL* tcg2; /* TCG2 protocol resolved in main */
}
globals_t;

/* set immediately by efi_main() */
extern globals_t globals;

#endif /* _CVMBOOT_BOOTLOADER_GLOBALS_H */
