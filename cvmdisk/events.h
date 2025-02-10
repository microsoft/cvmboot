// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_EVENTS_H
#define _CVMBOOT_CVMDISK_EVENTS_H

#include <utils/sha256.h>
#include <utils/events.h>
#include "sig.h"

#define MAX_PCRS 24
#define MAX_PCR_LOG_EVENTS 16

typedef struct _tcg_log_event
{
    int pcrnum;
    sha256_t digest;
}
tcg_log_event_t;

typedef struct _process_events_callback_data
{
    sha256_t sha256_pcrs[MAX_PCRS];
    tcg_log_event_t events[MAX_PCR_LOG_EVENTS];
    size_t num_events;
}
process_events_callback_data_t;

int preprocess_events(const char* events_path, const char* signer);

int process_events(
    const char* events_path,
    const char* signer,
    process_events_callback_data_t* cbd);

#endif /* _CVMBOOT_CVMDISK_EVENTS_H */
