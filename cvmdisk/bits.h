// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_BITS_H
#define _CVMBOOT_CVMDISK_BITS_H

#include <stdint.h>
#include <stddef.h>

static inline bool test_bit(const uint8_t* data, size_t index)
{
    const size_t byte = index / 8;
    const size_t bit = index % 8;
    return ((size_t)(data[byte]) & (1 << bit)) ? 1 : 0;
}

static inline void set_bit(uint8_t* data, size_t index)
{
    const size_t byte = index / 8;
    const size_t bit = index % 8;
    data[byte] |= (1 << bit);
}

static inline void clear_bit(uint8_t* data, size_t index)
{
    const size_t byte = index / 8;
    const size_t bit = index % 8;
    data[byte] &= ~(1 << bit);
}

#endif /* _CVMBOOT_CVMDISK_BITS_H */
