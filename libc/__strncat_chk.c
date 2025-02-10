// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char * __strncat_chk(char * s1, const char * s2, size_t n, size_t s1len)
{
    return strncat(s1, s2, n);
}
