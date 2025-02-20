// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_OPTIONS_H
#define _CVMBOOT_CVMDISK_OPTIONS_H

#include <stdbool.h>

typedef struct
{
    bool help;
    bool verbose;
    bool trace;
    bool etrace;
    bool version;
}
options_t;

extern options_t g_options;

#endif /* _CVMBOOT_CVMDISK_OPTIONS_H */
