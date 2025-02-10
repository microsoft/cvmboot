// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "efivar.h"

int set_efi_var(
    const CHAR16* name,
    const EFI_GUID* guid,
    const void* value_data,
    size_t value_size)
{
    int ret = -1;
    UINT32 attrs = 0;

    attrs |= EFI_VARIABLE_NON_VOLATILE;
    attrs |= EFI_VARIABLE_BOOTSERVICE_ACCESS;
    attrs |= EFI_VARIABLE_RUNTIME_ACCESS;

    if (RT->SetVariable(
        (CHAR16*)name,
        (EFI_GUID*)guid,
        attrs,
        value_size,
        (void*)value_data) != 0)
    {
        goto done;
    }

    ret = 0;

done:
    return ret;
}
