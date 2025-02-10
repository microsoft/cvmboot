// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_EVENTS_H
#define _CVMBOOT_BOOTLOADER_EVENTS_H

#include <stdint.h>
#include "tpm2.h"

// Extend an event to the TCG-LOG and into the given PCR.
int hash_log_extend_event(
    uint32_t pcr,
    const void* data,
    size_t size,
    TCG_EVENTTYPE type,
    const void* log,
    size_t logsize);

int hash_log_extend_binary_event(uint32_t pcrnum, const char* data);

int process_events_callback(
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* data,
    const char* signer,
    void* callback_data);

#endif /* _CVMBOOT_BOOTLOADER_EVENTS_H */
