// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "err.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static const char* _arg0 = "unknown";
static bool _show_file_line_func;

void err_set_arg0(const char* arg0)
{
    _arg0 = arg0;
}

void err_show_file_line_func(bool flag)
{
    _show_file_line_func = flag;
}

static void _verr(
    const char* file,
    unsigned int line,
    const char* func,
    const char* fmt,
    va_list ap)
{
    if (_show_file_line_func)
        fprintf(stderr, "%s: %s(%u): %s(): error: ", _arg0, file, line, func);
    else
        fprintf(stderr, "%s: error: ", _arg0);

    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void __err(
    const char* file,
    unsigned int line,
    const char* func,
    const char* fmt,
    ...)
{
    va_list ap;
    va_start(ap, fmt);
    _verr(file, line, func, fmt, ap);
    va_end(ap);
    exit(1);
}

void __err_noexit(
    const char* file,
    unsigned int line,
    const char* func,
    const char* fmt,
    ...)
{
    va_list ap;
    va_start(ap, fmt);
    _verr(file, line, func, fmt, ap);
    va_end(ap);
}
