// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "system.h"

void system_reset()
{
    Print(L"Press any key to reboot...\n");
    Pause();

    if (RT->ResetSystem(EfiResetWarm, 0, 0, NULL) != EFI_SUCCESS)
    {
        for (;;)
            ;
    }
}
