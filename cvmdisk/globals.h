// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_GLOBALS_H
#define _CVMBOOT_CVMDISK_GLOBALS_H

#include <limits.h>
#include <stdbool.h>

typedef struct globals
{
    /* points to argv[2] */
    const char* disk;

    /* loopback device created by losetup from disk */
    char loop[PATH_MAX];
}
globals_t;

extern globals_t globals;

#endif /* _CVMBOOT_CVMDISK_GLOBALS_H */
