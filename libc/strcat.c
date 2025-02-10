// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char *strcat(char *dest, const char *str)
{
    return strcpy(dest + strlen(dest), str);
}
