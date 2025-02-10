// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

long int strtol(const char* nptr, char** endptr, int base)
{
    return (long int)strtoul(nptr, endptr, base);
}
