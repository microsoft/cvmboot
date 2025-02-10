// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_PROGRESS_H
#define _CVMBOOT_CVMDISK_PROGRESS_H

#include <stddef.h>
#include <stdlib.h>
#include "stopwatch.h"

typedef struct progress
{
    size_t last_percent;
    char msg[1024];
    stopwatch_t stopwatch;
}
progress_t;

void progress_start(progress_t* progress, const char* msg);

void progress_update(progress_t* progress, size_t i, size_t n);

void progress_end(progress_t* progress);

#endif /* _CVMBOOT_CVMDISK_PROGRESS_H */
