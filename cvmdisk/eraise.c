// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include "eraise.h"
#include "options.h"

void __eraise(const char* file, uint32_t line, const char* func, int errnum)
{
    if (g_options.etrace)
        fprintf(stderr, "%s(%u): %s(): %d\n", file, line, func, errnum);
}
