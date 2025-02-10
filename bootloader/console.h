// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_CONSOLE_H
#define _CVMBOOT_BOOTLOADER_CONSOLE_H

#include <efi.h>
#include <efiapi.h>
#include <eficon.h>
#include <efilib.h>

EFI_STATUS efi_clear_screen(void);

EFI_STATUS efi_set_colors(UINTN foreground, UINTN background);

void pause(const char* msg);

#endif /* _CVMBOOT_BOOTLOADER_CONSOLE_H */
