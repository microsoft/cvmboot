// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_STOPWATCH_H
#define _CVMBOOT_CVMDISK_STOPWATCH_H

typedef struct stopwatch
{
    __uint128_t start;
    __uint128_t end;
}
stopwatch_t;

void stopwatch_start(stopwatch_t* stopwatch);

void stopwatch_stop(stopwatch_t* stopwatch);

void stopwatch_print(stopwatch_t* stopwatch);

double stopwatch_seconds(stopwatch_t* stopwatch);

#endif /* _CVMBOOT_CVMDISK_STOPWATCH_H */
