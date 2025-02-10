// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

size_t strcspn(const char* s, const char* reject)
{
    const char* p = s;

    while (*p)
    {
        if (strchr(reject, *p))
            break;
        p++;
    }

    return (size_t)(p - s);
}
