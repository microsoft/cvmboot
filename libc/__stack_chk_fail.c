// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>

void __stack_chk_fail()
{
    Print(L"%a(): error\n", __FUNCTION__);

    for (;;)
        ;
}
