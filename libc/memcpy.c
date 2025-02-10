// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void* memcpy(void* dest, const void* src, size_t n)
{
    uint8_t* p = (uint8_t*)dest;
    const uint8_t* q = (const uint8_t*)src;

    /* if dest and src are 8-byte aligned */
    if (((uint64_t)p & 0x0000000000000007) == 0 &&
        ((uint64_t)q & 0x0000000000000007) == 0)
    {
        uint64_t* pp = (uint64_t*)p;
        const uint64_t* qq = (const uint64_t*)q;
        size_t nn = n / 8;

#ifdef USE_LOOP_UNROLLING

        while (nn >= 8)
        {
            pp[0] = qq[0];
            pp[1] = qq[1];
            pp[2] = qq[2];
            pp[3] = qq[3];
            pp[4] = qq[4];
            pp[5] = qq[5];
            pp[6] = qq[6];
            pp[7] = qq[7];
            pp += 8;
            qq += 8;
            nn -= 8;
        }

#endif /* USE_LOOP_UNROLLING */

        while (nn--)
            *pp++ = *qq++;

        n %= 8;
        p = (uint8_t*)pp;
        q = (const uint8_t*)qq;
    }

    while (n--)
        *p++ = *q++;

    return dest;
}
