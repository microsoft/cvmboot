// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include <stdio.h>

extern CHAR16* __alloc_format16(const char* format);

int snprintf(char* str, size_t size, const char* format, ...)
{
    int ret = -1;
    CHAR16* format16 = NULL;
    CHAR16* str16 = NULL;
    size_t i;

    if (!str || !format)
        goto done;

    if (size == 0)
    {
        ret = 0;
        goto done;
    }

    /* allocate a CHAR16 format string */
    if (!(format16 = __alloc_format16(format)))
        goto done;

    /* allocate a zero-filled CHAR16 buffer */
    if (!(str16 = calloc(size, sizeof(CHAR16))))
        goto done;

    /* call EFI VPrint() to print the string */
    {
        va_list ap;
        va_start(ap, format);
        ret = (int)VSPrint(str16, size * sizeof(CHAR16), format16, ap);
        va_end(ap);
    }

    /* convert 16-bit string back to 8-bit string */
    for (i = 0; i < size; i++)
        str[i] = (char)str16[i];

done:

    if (format16)
        free(format16);

    if (str16)
        free(str16);

    return ret;
}
