// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include "exec.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <utils/strings.h>
#include <common/err.h>
#include "err.h"

static bool _trace;

void execf_set_trace(bool flag)
{
    _trace = flag;
}

static int _vexecf(buf_t* buf, bool exit_on_error, const char* fmt, va_list ap)
{
    FILE* is;
    char* cmd;
    size_t n;
    char buffer[4096];
    int status;

    buf_clear(buf);

    if (vasprintf(&cmd, fmt, ap) < 0)
        ERR("out of memory");

    if (_trace)
        printf("%s(): %s\n", __FUNCTION__, cmd);

    if (!(is = popen(cmd, "r")))
        ERR("popen() failed: %s: err=%s", cmd, strerror(errno));

    while ((n = fread(buffer, 1, sizeof(buffer), is)) != 0)
    {
        if (buf_append(buf, buffer, n) < 0)
            ERR("out of memory");

        if (_trace)
            printf("%.*s\n", (int)n, buffer);
    }

    /* null terminate buffer */
    buf_append(buf, "\0", 1);

    strrtrim((char*)buf->data);

    if ((status = pclose(is)))
    {
        if (exit_on_error)
            ERR("Command failed: %s: returned %d\n", cmd, status);
    }

    free(cmd);

    return status;
}

int execf(buf_t* buf, const char* fmt, ...)
{
    va_list ap;
    int status;

    va_start(ap, fmt);
    status = _vexecf(buf, true, fmt, ap);
    va_end(ap);

    return status;
}

int execf_return(buf_t* buf, const char* fmt, ...)
{
    va_list ap;
    int status;

    va_start(ap, fmt);
    status = _vexecf(buf, false, fmt, ap);
    va_end(ap);

    return status;
}
