// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char *strncpy(char *dest, const char *src, size_t n)
{
    char* p = dest;

    while (n-- && *src)
        *p++ = *src++;

    *p = '\0';

    return dest;
}
