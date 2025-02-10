// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdarg.h>
#include <stdio.h>
#include "err.h"

void err_format(err_t* err, const char* fmt, ...)
{
    if (err && fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->buf, sizeof(err->buf), fmt, ap);
        va_end(ap);
    }
}
