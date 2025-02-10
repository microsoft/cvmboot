// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "console.h"

EFI_STATUS efi_clear_screen(void)
{
    EFI_STATUS status = EFI_SUCCESS;

    if ((status = uefi_call_wrapper(ST->ConOut->ClearScreen, 1, ST->ConOut)) !=
        EFI_SUCCESS)
    {
        goto done;
    }

done:

    return status;
}

EFI_STATUS efi_set_colors(UINTN foreground, UINTN background)
{
#if 0
    EFI_STATUS status = EFI_SUCCESS;
    UINTN colors = EFI_TEXT_ATTR(foreground, background);

    if ((status = uefi_call_wrapper(
             ST->ConOut->SetAttribute, 2, ST->ConOut, colors)) != EFI_SUCCESS)
    {
        goto done;
    }

done:

    return status;
#else
    return EFI_SUCCESS;
#endif
}

void pause(const char* msg)
{
    if (msg)
        Print(L"%a: Press any key to continue...\n", msg);
    else
        Print(L"Press any key to continue...\n");

    Pause();
}
