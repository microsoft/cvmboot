// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stddef.h>

char *strpbrk(const char *dest, const char *breakset)
{
    const char *p;
    for (; *dest; dest++) {
        for (p = breakset; *p; p++) {
            if (*dest == *p)
                return (char *)dest;
        }
    }
    return NULL;
}