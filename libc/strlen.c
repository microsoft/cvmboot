// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

size_t strlen(const char* s)
{
    size_t n = 0;

    while (*s++)
        n++;

    return n;
}
