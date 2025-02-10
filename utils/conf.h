// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_CONF_H
#define _CVMBOOT_UTILS_CONF_H

#include <stddef.h>
#include "err.h"

typedef int (*conf_callback_t)(
    const char* name,
    const char* value,
    void* callbackData,
    err_t* err);

/* parse files with name=value pairs */
int conf_parse(
    const char* text,
    unsigned long textSize,
    conf_callback_t callback,
    void* callbackData,
    unsigned int* errorLine,
    err_t* err);

#endif /* _CVMBOOT_UTILS_CONF_H */
