// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "sleep.h"

EFI_STATUS Sleep(const UINT64 seconds)
{
    EFI_STATUS status = EFI_SUCCESS;
    EFI_EVENT event = NULL;
    UINTN index = 0;

    /* Create an EVT_TIMER event */
    if ((status = BS->CreateEvent(
        EVT_TIMER,
        0,
        NULL,
        NULL,
        &event)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Set the timer to go off once */
    if ((status = BS->SetTimer(
        event,
        TimerPeriodic,
        10000000 * seconds)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Wait for the timer event */
    if ((status = BS->WaitForEvent(1, &event, &index)) != EFI_SUCCESS)
    {
        goto done;
    }

done:

    if (event)
        BS->CloseEvent(event);

    return status;
}
