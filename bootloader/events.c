// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "events.h"
#include <string.h>
#include <stdio.h>
#include <utils/hexstr.h>
#include <utils/json.h>
#include <utils/strings.h>
#include <utils/allocator.h>
#include <utils/events.h>
#include "console.h"
#include "system.h"

int hash_log_extend_event(
    uint32_t pcr,
    const void* data,
    size_t size,
    TCG_EVENTTYPE type,
    const void* log,
    size_t logsize)
{
    int ret = -1;
    EFI_TCG2_EVENT *event = NULL;
    UINTN event_size = sizeof(EFI_TCG2_EVENT) - sizeof(event->Event) + logsize;

    if (!(event = __allocator.alloc(event_size)))
        goto done;

    memset(event, 0, event_size);
    event->Header.HeaderSize = sizeof(EFI_TCG2_EVENT_HEADER);
    event->Header.HeaderVersion = 1;
    event->Header.PCRIndex = pcr;
    event->Header.EventType = type;
    event->Size = event_size;
    CopyMem(event->Event, log, logsize);

    if (TCG2_HashLogExtendEvent(
        NULL, 0, (EFI_PHYSICAL_ADDRESS)data, size, event) != EFI_SUCCESS)
    {
        goto done;
    }

    ret = 0;
done:

    if (event)
        __allocator.free(event);

    return ret;
}

int hash_log_extend_binary_event(uint32_t pcrnum, const char* data)
{
    int ret = -1;
    void* bindata = NULL;
    size_t binsize = strlen(data) / 2;

    if (!(bindata = __allocator.alloc(binsize)))
        goto done;

    if (hexstr_scan(data, bindata, binsize) != binsize)
        goto done;

    if (hash_log_extend_event(pcrnum, bindata, binsize, EV_COMPACT_HASH,
        bindata, binsize) != 0)
    {
        goto done;
    }

    ret = 0;

done:

    if (bindata)
        __allocator.free(bindata);

    return ret;
}

int process_events_callback(
    size_t index,
    uint32_t pcrnum,
    const char* type,
    const char* data,
    const char* signer,
    void* callback_data)
{
    int ret = -1;
    const size_t size = strlen(data) + 1;

    if (strcmp(type, "string") == 0)
    {
        if (hash_log_extend_event(pcrnum, data, size, EV_IPL, data, size) != 0)
            goto done;
    }
    else if (strcmp(type, "binary") == 0)
    {
        if (hash_log_extend_binary_event(pcrnum, data) < 0)
            goto done;
    }

    ret = 0;

done:

    return ret;
}
