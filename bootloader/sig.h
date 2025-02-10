// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_SIG_H
#define _CVMBOOT_BOOTLOADER_SIG_H

#include <utils/sig.h>
#include <stddef.h>

int sig_verify(const sig_t* sig);

#endif /* _CVMBOOT_BOOTLOADER_SIG_H */
