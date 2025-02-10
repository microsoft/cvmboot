// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>

int usleep(long usec)
{
    Print(L"usleep: %lu\n", usec);
    return 0;
}

