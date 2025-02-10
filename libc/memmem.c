// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdint.h>
#include <string.h>

void* memmem(
    const void* haystack,
    size_t haystacklen,
    const void* needle,
    size_t needlelen)
{
    size_t i;

    if (needlelen <= haystacklen)
    {
        for (i = 0; i <= haystacklen - needlelen; i++)
        {
            uint8_t* p = (UINT8*)haystack + i;

            if (memcmp(p, needle, needlelen) == 0)
                return p;
        }
    }

    /* Not found */
    return NULL;
}
