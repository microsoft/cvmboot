// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "events.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <utils/json.h>
#include <utils/events.h>
#include <utils/hexstr.h>
#include <common/err.h>
#include <common/file.h>

typedef struct _callback_data
{
    const char* signer;
    identity_t identity;
}
callback_data_t;

static int _process_events_callback(
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* data,
    const char* signer,
    void* callback_data)
{
    process_events_callback_data_t* cbd = callback_data;
    size_t size = strlen(data) + 1;
    void* bindata = NULL;

    /* binary data requires conversion of string hash to binary hash */
    if (strcmp(type, "binary") == 0)
    {
        size_t binsize = strlen(data) / 2;

        if (!(bindata = malloc(binsize)))
            ERR("out of memory");

        if (hexstr_scan(data, bindata, binsize) != binsize)
            ERR("invalid binary data: %s", data);

        /* Reset <data:size> to point to binary data */
        data = bindata;
        size = binsize;
    }

    /* measure and extend data */
    {
        sha256_t digest;
        sha256_string_t str;

        sha256_compute(&digest, data, size);
        sha256_extend(&cbd->sha256_pcrs[pcrnum], &digest);
        sha256_format(&str, &digest);

        if (cbd->num_events == MAX_PCR_LOG_EVENTS)
        {
            ERR("too many pcr log events in events file (> %d)",
                MAX_PCR_LOG_EVENTS);
        }

        cbd->events[cbd->num_events].pcrnum = pcrnum;
        cbd->events[cbd->num_events].digest = digest;
        cbd->num_events++;
    }

    if (bindata)
        free(bindata);

    return 0;
}

int process_events(
    const char* events_path,
    const char* signer,
    process_events_callback_data_t* cbd)
{
    int ret = -1;
    unsigned int error_line;
    err_t err;

    memset(cbd, 0, sizeof(process_events_callback_data_t));

    if (*events_path)
    {
        char* text = NULL;
        size_t text_size;

        if (load_file(events_path, (void**)&text, &text_size) != 0)
            ERR("failed to load events file: %s", events_path);

        /* Calculate expected PCR-11 values from the events file */
        if (parse_events_file(
            text,
            text_size,
            signer,
            _process_events_callback,
            cbd,
            &error_line,
            &err) != 0)
        {
            ERR("failed to parse events: %s: %s", events_path, err.buf);
        }

        free(text);
    }

    ret = 0;

    return ret;
}

static int _preprocess_events_callback(
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* data,
    const char* signer,
    void* callback_data)
{
    return 0;
}

int preprocess_events(const char* events_path, const char* signer)
{
    int ret = -1;
    unsigned int error_line;
    err_t err;

    if (*events_path)
    {
        char* text = NULL;
        size_t text_size;

        if (load_file(events_path, (void**)&text, &text_size) != 0)
            ERR("failed to load events file: %s", events_path);

        /* Calculate expected PCR-11 values from the events file */
        if (parse_events_file(
            text,
            text_size,
            signer,
            _preprocess_events_callback,
            NULL,
            &error_line,
            &err) != 0)
        {
            ERR("failed to parse events: %s: %s", events_path, err.buf);
        }

        free(text);
    }

    ret = 0;

    return ret;
}
