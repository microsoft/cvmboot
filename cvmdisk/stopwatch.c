// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "stopwatch.h"
#include "colors.h"
#include <stddef.h>
#include <stdio.h>
#include <sys/time.h>

typedef __uint128_t u128_t;
static const u128_t _usec = 1000000;

void stopwatch_start(stopwatch_t* stopwatch)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    stopwatch->start = ((u128_t)tv.tv_sec * _usec) + (u128_t)tv.tv_usec;
    stopwatch->end = 0;
}

void stopwatch_stop(stopwatch_t* stopwatch)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    stopwatch->end = ((u128_t)tv.tv_sec * _usec) + (u128_t)tv.tv_usec;
}

double stopwatch_seconds(stopwatch_t* stopwatch)
{
    stopwatch_stop(stopwatch);
    u128_t delta = stopwatch->end - stopwatch->start;
    return (double)delta / (double)_usec;
}

void stopwatch_print(stopwatch_t* stopwatch)
{
    stopwatch_stop(stopwatch);
    u128_t delta = stopwatch->end - stopwatch->start;
    printf("%s%.2lf seconds%s\n",
        colors_yellow, (double)delta / (double)_usec, colors_reset);
}
