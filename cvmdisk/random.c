// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdint.h>
#include <stddef.h>

static uint64_t _rdrand()
{
    uint64_t r = 0;

    while (r == 0)
        asm("rdrand %0\n\t" :"=a"(r)::);

    return r;
}

int get_random_bytes(void* data, size_t size)
{
    int ret = -1;
    union
    {
        uint64_t word;
        uint8_t bytes[sizeof(uint64_t)];
    }
    u;
    const size_t m = (size + sizeof(uint64_t) - 1) / sizeof(uint64_t);
    uint8_t* p = (uint8_t*)data;
    size_t r = size;

    if (!data)
        goto done;

    for (size_t i = 0; i < m && r > 0; i++)
    {
        const size_t min = (r < sizeof(uint64_t)) ? r : sizeof(uint64_t);
        u.word = _rdrand();

        for (size_t j = 0; j < min; j++)
            p[j] = u.bytes[j];

        r -= min;
        p += min;
    }

    ret = 0;

done:
    return ret;
}
