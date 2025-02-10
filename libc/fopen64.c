// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include "panic.h"

FILE *fopen64(const char *filename, const char *type)
{
    Print(L"fopen64(%s, %s)\n", filename, type);
    return NULL;
}

