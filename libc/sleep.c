// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>

unsigned int sleep(unsigned int seconds)
{
    Print(L"sleep: %u\n", seconds);
    return 0;
}

