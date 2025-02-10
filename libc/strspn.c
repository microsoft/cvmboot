// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

size_t strspn(const char* s, const char* accept)
{
    const char* p = s;

    while (*p)
    {
        if (!strchr(accept, *p))
            break;
        p++;
    }

    return (size_t)(p - s);
}
