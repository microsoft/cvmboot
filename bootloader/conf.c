// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "conf.h"
#include <string.h>
#include <utils/err.h>
#include <utils/conf.h>
#include "cpio.h"

static int _callback(
    const char* name,
    const char* value,
    void* callback_data,
    err_t* err)
{
    int ret = -1;
    conf_t* conf = callback_data;

    if (strcmp(name, "cmdline") == 0)
    {
        strlcpy(conf->cmdline, value, sizeof(conf->cmdline));
    }
    else if (strcmp(name, "roothash") == 0)
    {
        if (strlen(value) != SHA256_STRING_LENGTH)
        {
            err_format(err, "malformed roothash option (wrong length)");
            goto done;
        }

        if (sha256_scan(value, &conf->roothash) < 0)
        {
            err_format(err, "malformed roothash option (not hex string)");
            goto done;
        }
    }
    else if (strcmp(name, "kernel") == 0)
    {
        strlcpy(conf->kernel, value, sizeof(conf->kernel));
    }
    else if (strcmp(name, "initrd") == 0)
    {
        strlcpy(conf->initrd, value, sizeof(conf->initrd));
    }
    else if (strcmp(name, "timestamp") == 0)
    {
        /* ignore */
    }
    else
    {
        err_format(err, "unknown configuration file key: %s", name);
        goto done;
    }

    ret = 0;

done:
    return ret;
}

int conf_load(
    const void* cpio_data,
    size_t cpio_size,
    conf_t* conf,
    err_t* err)
{
    int ret = -1;
    void* data = NULL;
    size_t size;
    unsigned int error_line;

    err_clear(err);

    if (cpio_load_file(
        cpio_data, cpio_size, FILENAME_CVMBOOT_CONF, &data, &size) != 0)
    {
        err_format(err, "failed to load cvmboot.conf file");
        goto done;
    }

    if (conf_parse(data, size, _callback, conf, &error_line, err) < 0)
        goto done;

    ret = 0;

done:
    return ret;
}
