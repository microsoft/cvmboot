// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

void* memchr(const void *s, int c, size_t n)
{
    size_t i;
    const char* p = (const char*)s;

    for (i = 0; i < n; i++)
    {
        if (p[i] == c)
            return (void*)&p[i];
    }

    return NULL;
}
