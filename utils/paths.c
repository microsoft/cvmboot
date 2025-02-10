// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "paths.h"
#include "strings.h"

static char _prefix[PATH_MAX];

static const char* _lookup(pathid_t id)
{
    switch (id)
    {
        case FILENAME_EVENTS:
            return "/EFI/cvmboot/events";
        case FILENAME_CVMBOOT_CONF:
            return"/EFI/cvmboot/cvmboot.conf";
        case FILENAME_CVMBOOT_CPIO:
            return"/EFI/cvmboot.cpio";
        case FILENAME_CVMBOOT_CPIO_SIG:
            return"/EFI/cvmboot.cpio.sig";
        case DIRNAME_CVMBOOT_HOME:
            return "/EFI/cvmboot";
    }

    return NULL;
}

void paths_set_prefix(const char* prefix)
{
    strlcpy(_prefix, prefix, sizeof(_prefix));
}

const char* paths_get(char path[PATH_MAX], pathid_t id, const char* rootdir)
{
    *path = '\0';

    if (rootdir && *rootdir)
        strlcat(path, rootdir, PATH_MAX);

    if (*_prefix)
        strlcat(path, _prefix, PATH_MAX);

    strlcat(path, _lookup(id), PATH_MAX);

    return path;
}

const uint16_t* paths_convert(uint16_t* wpath, const char* path)
{
    const char* p = path;
    uint16_t* q = wpath;

    while (*p)
    {
        char c = *p++;

        if (c == '/')
            c = '\\';

        *q++ = c;
    }

    *q = '\0';

    return wpath;
}
