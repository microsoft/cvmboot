// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <efi.h>
#include <efilib.h>

typedef signed long long time_t;

struct tm
{
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long __tm_gmtoff;
    const char *__tm_zone;
};

#endif /* _TIME_H */
