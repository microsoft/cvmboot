// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_STATUS_H
#define _CVMBOOT_UTILS_STATUS_H

#include <efi.h>
#include <efilib.h>

const char* efi_strerror(EFI_STATUS status);

void efi_puterr(EFI_STATUS status);

#endif /* _CVMBOOT_UTILS_STATUS_H */
