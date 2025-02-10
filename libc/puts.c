// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdio.h>

int puts(const char *s)
{
    Print(L"%a\n", s);
    return 0;
}
