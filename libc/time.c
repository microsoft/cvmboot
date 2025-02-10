// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include <time.h>
#include <stddef.h>
#include <string.h>
#include "panic.h"

static UINT64 _secs_since_epoch(
    UINT64 year,
    UINT64 month,
    UINT64 day,
    UINT64 hours,
    UINT64 minutes,
    UINT64 seconds)
{
    UINT64 tmp = (14 - month) / 12;
    UINT64 y = year + 4800 - tmp;
    UINT64 m = month + 12 * tmp - 3;
    UINT64 julianDays =
        day +
        ((153 * m + 2) / 5) +
        (y * 365) +
        (y / 4) -
        (y / 100) +
        (y / 400) -
        32045;
    UINT64 posixDays = julianDays - 2440588;

    return
        (posixDays * 24 * 60 * 60) +
        (hours * 60 * 60) +
        (minutes * 60) +
        seconds;
}

time_t time(time_t *timer)
{
    EFI_TIME time;
    time_t t;

    if (timer)
        *timer = 0;

    if (uefi_call_wrapper(RT->GetTime, 2, &time, NULL) != EFI_SUCCESS)
    {
        LIBC_PANIC;
        return 0;
    }

    t = _secs_since_epoch(
        time.Year,
        time.Month,
        time.Day,
        time.Hour,
        time.Minute,
        time.Second);

    if (timer)
        *timer = t;

    return t;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    EFI_TIME time;
    struct tm t;

    if (!timep || !result)
    {
        LIBC_PANIC;
        return NULL;
    }

    if (uefi_call_wrapper(RT->GetTime, 2, &time, NULL) != EFI_SUCCESS)
    {
        LIBC_PANIC;
        return NULL;
    }

    /* Initialze fields supported by EFI */
    t.tm_sec = time.Second;
    t.tm_min = time.Minute;
    t.tm_hour = time.Hour;
    t.tm_mday = time.Day;
    t.tm_mon = time.Month;
    t.tm_year = time.Year;
    t.tm_isdst = time.Daylight;

    /* Currently openssl does not use these fields */
    t.__tm_gmtoff = 0;
    t.__tm_zone = 0;

    return memcpy(result, &t, sizeof(t));
}

struct tm *localtime(const time_t *timep)
{
    LIBC_PANIC;
    return NULL;
}
