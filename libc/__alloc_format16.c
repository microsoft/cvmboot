// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

CHAR16* __alloc_format16(const char* format)
{
    size_t len;
    CHAR16* format16;

    if (!format)
        return NULL;

    len = strlen(format);

    /* allocate a CHAR16 format string */
    if (!(format16 = malloc((len + 1) * sizeof(CHAR16))))
        return NULL;

    /* copy format to format16 */
    {
        size_t i;

        for (i = 0; i < len; i++)
            format16[i] = format[i];

        format16[len] = '\0';
    }

    /* translate '%s' to '%a' */
    {
        CHAR16* p = format16;

        while (*p)
        {
            if (p[0] == '%' && p[1] == 's')
            {
                p[1] = 'a';
                p += 2;
                continue;
            }

            p++;
        }
    }

    return format16;
}
