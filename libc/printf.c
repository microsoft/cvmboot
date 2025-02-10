// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include <stdio.h>

extern CHAR16* __alloc_format16(const char* format);

int printf(const char *format, ...)
{
    int ret = -1;
    CHAR16* format16 = NULL;

    if (!format)
        goto done;

    /* allocate a CHAR16 format string */
    if (!(format16 = __alloc_format16(format)))
        goto done;

    /* call EFI VPrint() to print the string */
    {
        va_list ap;
        va_start(ap, format);
        ret = (int)VPrint(format16, ap);
        va_end(ap);
    }

done:

    if (format16)
        free(format16);

    return ret;
}
