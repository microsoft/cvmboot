// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include <stdlib.h>

extern CHAR16* __alloc_format16(const char* format);

int vprintf(const char *format, va_list ap)
{
    int ret = -1;
    CHAR16* format16 = NULL;

    if (!format)
        goto done;

    /* allocate a CHAR16 format string */
    if (!(format16 = __alloc_format16(format)))
        goto done;

    /* call EFI VPrint() to print the string */
    ret = (int)VPrint(format16, ap);

done:

    if (format16)
        free(format16);

    return ret;
}
