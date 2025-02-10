// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "cpio.h"
#include <string.h>
#include <utils/cpio.h>
#include <stdbool.h>
#include "globals.h"

static int _cpio_load_file(
    const void* cpio_data,
    size_t cpio_size,
    pathid_t id,
    void** data,
    size_t* size,
    bool direct)
{
    int ret = -1;
    char home[PATH_MAX];
    char path[PATH_MAX];
    const char* p;

    *data = NULL;
    *size = 0;

    if (!cpio_data || !cpio_size)
        goto done;

    paths_get(home, DIRNAME_CVMBOOT_HOME, NULL);
    paths_get(path, id, NULL);

    if (strncmp(path, home, strlen(home)) != 0)
        goto done;

    p = path + strlen(home);

    if (*p == '/')
        p++;

    if (direct)
    {
        if (cpio_get_file_direct(
            cpio_data, cpio_size, p, (const void**)data, size) < 0)
        {
            goto done;
        }
    }
    else
    {
        if (cpio_get_file(cpio_data, cpio_size, p, data, size) < 0)
        {
            goto done;
        }
    }

    ret = 0;

done:
    return ret;
}

static int _cpio_load_file_by_name(
    const void* cpio_data,
    size_t cpio_size,
    const char* name,
    void** data,
    size_t* size,
    bool direct)
{
    int ret = -1;

    *data = NULL;
    *size = 0;

    if (!cpio_data || !cpio_size)
        goto done;

    if (direct)
    {
        if (cpio_get_file_direct(
            cpio_data, cpio_size, name, (const void**)data, size) < 0)
        {
            goto done;
        }
    }
    else
    {
        if (cpio_get_file(cpio_data, cpio_size, name, data, size) < 0)
        {
            goto done;
        }
    }

    ret = 0;

done:
    return ret;
}

int cpio_load_file(
    const void* cpio_data,
    size_t cpio_size,
    pathid_t id,
    void** data,
    size_t* size)
{
    return _cpio_load_file(cpio_data, cpio_size, id, data, size, false);
}

int cpio_load_file_direct(
    const void* cpio_data,
    size_t cpio_size,
    pathid_t id,
    const void** data,
    size_t* size)
{
    return _cpio_load_file(cpio_data, cpio_size, id, (void**)data, size, true);
}

int cpio_load_file_by_name(
    const void* cpio_data,
    size_t cpio_size,
    const char* name,
    void** data,
    size_t* size)
{
    return _cpio_load_file_by_name(
        cpio_data, cpio_size, name, data, size, false);
}

int cpio_load_file_direct_by_name(
    const void* cpio_data,
    size_t cpio_size,
    const char* name,
    const void** data,
    size_t* size)
{
    return _cpio_load_file_by_name(
        cpio_data, cpio_size, name, (void**)data, size, true);
}
