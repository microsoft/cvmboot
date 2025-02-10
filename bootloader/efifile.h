// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_EFIFILE_H
#define _CVMBOOT_BOOTLOADER_EFIFILE_H

#include <efi.h>

typedef struct _efi_file efi_file_t;

efi_file_t* efi_file_open(
    EFI_HANDLE imageHandle,
    const CHAR16* path,
    IN UINT64 fileMode,
    IN BOOLEAN append);

EFI_STATUS efi_file_read(
    IN efi_file_t* efiFile,
    IN void* data,
    IN UINTN size,
    IN UINTN* sizeRead);

EFI_STATUS efi_file_close(efi_file_t* efiFile);

EFI_STATUS efi_file_load(
    IN EFI_HANDLE imageHandle,
    IN const CHAR16* path,
    OUT void** data,
    OUT UINTN* size);

#endif /* _CVMBOOT_BOOTLOADER_EFIFILE_H */
