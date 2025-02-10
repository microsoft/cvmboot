// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdio.h>

int putchar(int c)
{
    Print(L"%c", (CHAR16)c);
    return 1;
}
