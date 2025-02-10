// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "strings.h"
#include <stdint.h>
#include <stdio.h>

typedef __uint128_t u128_t;

static bool _all_zeros_u128(const u128_t* s, size_t n)
{
    while (n > 0 && *s == 0)
    {
        s++;
        n--;
    }

    return n == 0;
}

static bool _all_zeros_u8(const uint8_t* s, size_t n)
{
    while (n > 0 && *s == 0)
    {
        s++;
        n--;
    }

    return n == 0;
}

bool all_zeros(const void* s, size_t n)
{
    if (((ptrdiff_t)s % sizeof(u128_t)) == 0 && (n % sizeof(u128_t)) == 0)
        return _all_zeros_u128((const u128_t*)s, n / sizeof(u128_t));

    return _all_zeros_u8((const uint8_t*)s, n);
}
