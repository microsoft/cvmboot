// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>

void __libc_warn(const char* msg)
{
    Print(L"__libc_warn(): %a\n", msg);
    for (;;)
        ;
}

void __libc_panic(const char* func, unsigned int line)
{
    Print(L"__libc_panic(): %a():%u\n", func, line);
    for (;;)
        ;
}
