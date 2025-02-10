// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdio.h>

int __snprintf_chk(
    char* str,
    size_t maxlen,
    int flag,
    size_t strlen,
    const char* format,
    ...)
{
    (void)flag;

    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, maxlen, format, ap);
    va_end(ap);

    return ret;
}
