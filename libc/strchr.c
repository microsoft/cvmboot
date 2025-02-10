// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char* strchr(const char* s, int c)
{
    while (*s && *s != c)
        s++;

    if (*s == c)
        return (char*)s;

    return NULL;
}
