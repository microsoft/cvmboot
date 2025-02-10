// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_ROUND_H
#define _CVMBOOT_CVMDISK_ROUND_H

#include <stdint.h>

static inline uint64_t round_up_to_multiple(uint64_t x, uint64_t m)
{
    return (x + m - 1) / m * m;
}

#endif /* _CVMBOOT_CVMDISK_ROUND_H */
