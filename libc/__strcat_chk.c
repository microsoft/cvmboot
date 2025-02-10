// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char *__strcat_chk(char *dest, const char *src, size_t destlen)
{
    return strcat(dest, src);
}
