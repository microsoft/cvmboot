// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char* strncat(char* dest, const char* src, size_t n)
{
    char* p = dest + strlen(dest);
    const char* q = src;
    size_t i;

    for (i = 0; i < n && *q; i++)
        *p++ = *q++;

    *p = '\0';

    return dest;
}
