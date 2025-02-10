// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_PATH_H
#define _CVMBOOT_CVMDISK_PATH_H

#include <limits.h>

typedef struct path
{
    char buf[PATH_MAX];
}
path_t;

const char* makepath2(
    path_t* path,
    const char* s1,
    const char* s2);

const char* makepath3(
    path_t* path,
    const char* s1,
    const char* s2,
    const char* s3);

const char* makepath4(
    path_t* path,
    const char* s1,
    const char* s2,
    const char* s3,
    const char* s4);

#endif /* _CVMBOOT_CVMDISK_PATH_H */
