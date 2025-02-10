// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_EVENTS_H
#define _CVMBOOT_UTILS_EVENTS_H

#include <stddef.h>
#include <stdint.h>
#include "err.h"

/*
"os-image-identity":{"signer":"%s",svn":"%s","diskId":"%s","eventVersion":"%s"}
*/
typedef struct identity
{
    char* svn;
    char* diskId;
    char* eventVersion;
}
identity_t;

typedef int (*process_events_callback_t)(
    size_t index,
    uint32_t pcr,
    const char* data,
    const char* type,
    const char* signer,
    void* callback_data);

int parse_events_file(
    const char* text,
    unsigned long text_size,
    const char* signer,
    process_events_callback_t callback,
    void* callback_data,
    unsigned int* error_line,
    err_t* err);

#endif /* _CVMBOOT_UTILS_EVENTS_H */
