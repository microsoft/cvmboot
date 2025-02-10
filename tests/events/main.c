// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <sys/stat.h>
#include <utils/events.h>
#include <utils/allocator.h>

allocator_t __allocator = { malloc, free };

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

static int _callback1(
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* data,
    const char* signer,
    void* callback_data)
{
#if 0
    printf("index{%zu}\n", index);
    printf("pcrnum{%u}\n", pcrnum);
    printf("type{%s}\n", type);
    printf("data{%s}\n", data);
#endif

    if (index == 0)
    {
        assert(pcrnum == 11);
        assert(strcmp(type, "string") == 0);
        assert(strcmp(data, "signer1=95b3fc4b2fba43ff82570c725f94edaa") == 0);
    }
    else if (index == 1)
    {
        assert(pcrnum == 23);
        assert(strcmp(type, "binary") == 0);
        assert(strcmp(data, "2dd1886c59504e609c1d089463f869c0") == 0);
    }
    else
    {
        assert("too many indices" == NULL);
    }

    return 0;
}

static int _callback2(
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* data,
    const char* signer,
    void* callback_data)
{
#if 0
    printf("index{%zu}\n", index);
    printf("pcrnum{%u}\n", pcrnum);
    printf("type{%s}\n", type);
    printf("data{%s}\n", data);
#endif

    if (index == 0)
    {
        assert(pcrnum == 11);
        assert(strcmp(type, "string") == 0);
    }
    else if (index == 1)
    {
        assert(pcrnum == 11);
        assert(strcmp(type, "string") == 0);
    }
    else
    {
        assert("too many indices" == NULL);
    }

    return 0;
}

int main(int argc, const char* argv[])
{
    void* data = NULL;
    size_t size = 0;
    const char signer[32] = "95b3fc4b2fba43ff82570c725f94edaa";
    unsigned int error_line = 0;
    err_t err = ERR_INITIALIZER;
    process_events_callback_t callback;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <events-file>\n", argv[0]);
        exit(1);
    }

    /* load events file into memory */
    if (load_file(argv[1], &data, &size) < 0)
        assert("failed to load events file" == 0);

    if (strcmp(argv[1], "events1") == 0)
        callback = _callback1;
    else if (strcmp(argv[1], "events2") == 0)
        callback = _callback2;
    else
        assert("unknown events file" == NULL);

    /* parse the events file */
    if (parse_events_file(
        data, size, signer, callback, NULL, &error_line, &err) < 0)
    {
        printf("err: %s\n", err.buf);
        assert("failed to parse events file" == 0);
    }

    printf("=== passed test (events %s)\n", argv[1]);

    return 0;
}
