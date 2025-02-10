// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char * __strncpy_chk(char * s1, const char * s2, size_t n, size_t s1len)
{
    return strncpy(s1, s2, n);
}
