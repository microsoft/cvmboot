// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdint.h>
#include <string.h>

void* memmove(void* dest_, const void* src_, size_t n)
{
    uint8_t* dest = (uint8_t*)dest_;
    const uint8_t* src = (const uint8_t*)src_;

    if (dest != src && n > 0)
    {
        if (dest <= src)
        {
            memcpy(dest, src, n);
        }
        else
        {
            for (src += n, dest += n; n--; dest--, src--)
                dest[-1] = src[-1];
        }
    }

    return dest_;
}
