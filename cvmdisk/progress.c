// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "progress.h"
#include <string.h>
#include <utils/strings.h>
#include <stdio.h>

void progress_start(progress_t* progress, const char* msg)
{
    memset(progress, 0, sizeof(progress_t));

    stopwatch_start(&progress->stopwatch);

    strlcpy(progress->msg, msg, sizeof(progress->msg));
    fprintf(stdout, "\r%s: %5.1lf%%", progress->msg, 0.0);
}

void progress_update(progress_t* progress, size_t i, size_t n)
{
    const size_t percent = (i * 1000) / n;

    if (percent != progress->last_percent)
    {
        fprintf(stdout, "\r%s: %5.1lf%%", progress->msg, (double)percent/10);
        fflush(stdout);
    }

    progress->last_percent = percent;
}

void progress_end(progress_t* progress)
{
    stopwatch_stop(&progress->stopwatch);
    double secs = stopwatch_seconds(&progress->stopwatch);
    fprintf(stdout, "\r%s: 100.0%% (%.2lf seconds)\n", progress->msg, secs);
    fflush(stdout);
}
