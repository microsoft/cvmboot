// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_PATHS_H
#define _CVMBOOT_UTILS_PATHS_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

typedef enum pathid
{
    FILENAME_EVENTS,
    FILENAME_CVMBOOT_CONF,
    FILENAME_CVMBOOT_CPIO,
    FILENAME_CVMBOOT_CPIO_SIG,
    DIRNAME_CVMBOOT_HOME,
}
pathid_t;

void paths_set_prefix(const char* prefix);

const char* paths_get(char path[PATH_MAX], pathid_t id, const char* rootdir);

const uint16_t* paths_convert(uint16_t* wpath, const char* path);

static inline const uint16_t* paths_getw(uint16_t path[PATH_MAX], pathid_t id)
{
    char buf[PATH_MAX];
    paths_get(buf, id, NULL);
    paths_convert(path, buf);
    return path;
}

#endif /* _CVMBOOT_UTILS_PATHS_H */
