// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void* memset(void* s, int c, size_t n)
{
    uint8_t* p = (uint8_t*)s;

    /* copy while more bytes and not 8-byte aligned */
    while (n && (((ptrdiff_t)p) & 0x000000000000000f))
    {
        *p++ = (uint8_t)c;
        n--;
    }

    /* if more bytes and p is 8-byte aligned */
    if (n > sizeof(__uint128_t) && ((ptrdiff_t)p & 0x000000000000000f) == 0)
    {
        __uint128_t* pp = (__uint128_t*)p;
        size_t nn = n / sizeof(__uint128_t);
        __uint128_t cc;

        memset(&cc, c, sizeof(cc));

        while (nn > 8)
        {
            pp[0] = cc;
            pp[1] = cc;
            pp[2] = cc;
            pp[3] = cc;
            pp[4] = cc;
            pp[5] = cc;
            pp[6] = cc;
            pp[7] = cc;
            pp += 8;
            nn -= 8;
        }

        while (nn > 0)
        {
            *pp++ = cc;
            nn--;
        }

        p = (uint8_t*)pp;
        n %= sizeof(__uint128_t);
    }

    /* handle remaining bytes if any */
    while (n > 0)
    {
        *p++ = (uint8_t)c;
        n--;
    }

    return s;
}
