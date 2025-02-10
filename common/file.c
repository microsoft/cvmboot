// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>
#include "file.h"

int load_file(const char* path, void** data_out, size_t* size_out)
{
    int ret = -1;
    ssize_t n;
    struct stat st;
    int fd = -1;
    void* data = NULL;
    uint8_t* p;
    struct locals
    {
        char block[512];
    };
    struct locals* locals = NULL;

    if (data_out)
        *data_out = NULL;

    if (size_out)
        *size_out = 0;

    if (!path || !data_out || !size_out)
        goto done;

    if (!(locals = malloc(sizeof(struct locals))))
        goto done;

    if ((fd = open(path, O_RDONLY, 0)) < 0)
        goto done;

    if (fstat(fd, &st) != 0)
        goto done;

    /* Allocate an extra byte for null termination */
    if (!(data = malloc((size_t)(st.st_size + 1))))
        goto done;

    p = data;

    /* Null-terminate the data */
    p[st.st_size] = '\0';

    while ((n = read(fd, locals->block, sizeof(locals->block))) > 0)
    {
        memcpy(p, locals->block, (size_t)n);
        p += n;
    }

    *data_out = data;
    data = NULL;
    *size_out = (size_t)st.st_size;

    ret = 0;

done:

    if (locals)
        free(locals);

    if (fd >= 0)
        close(fd);

    if (data)
        free(data);

    return ret;
}

static int _writen(int fd, const void* data, size_t size)
{
    int ret = -1;
    const uint8_t* p = (const uint8_t*)data;
    size_t r = size;

    while (r > 0)
    {
        ssize_t n;

        if ((n = write(fd, p, r)) <= 0)
        {
            goto done;
        }

        p += n;
        r -= (size_t)n;
    }

    ret = 0;

done:
    return ret;
}

int write_file(const char* path, const void* data, size_t size)
{
    int ret = -1;
    int fd = -1;

    if (!(fd = open(path, O_CREAT|O_RDWR, 0644)))
        goto done;

    if (_writen(fd, data, size) != 0)
        goto done;

    ret = 0;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}
