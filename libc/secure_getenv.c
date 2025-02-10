// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stddef.h>
#include <stdlib.h>
#include <efi.h>
#include <efilib.h>

char *secure_getenv(const char *name)
{
    Print(L"secure_getenv(): %a\n", name);
    return NULL;
}
