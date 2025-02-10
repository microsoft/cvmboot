// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include "malloc.h"
#include "panic.h"

#define SIZE_MAX ((size_t)(-1))

static int _is_pow2(size_t n)
{
    return (n != 0) && ((n & (n - 1)) == 0);
}

static uint64_t _round_up_to_multiple(uint64_t x, uint64_t m)
{
    return (x + m - 1) / m * m;
}

static size_t _get_padding_size(size_t alignment)
{
    if (!alignment)
        return 0;

    const size_t header_size = sizeof(malloc_header_t);
    return _round_up_to_multiple(header_size, alignment) - header_size;
}

static int _is_ptrsize_multiple(size_t n)
{
    size_t d = n / sizeof(void*);
    size_t r = n % sizeof(void*);
    return (d >= 1 && r == 0);
}

static size_t _calculate_block_size(size_t alignment, size_t size)
{
    size_t r = 0;
    r += _get_padding_size(alignment);
    r += sizeof(malloc_header_t);
    r += _round_up_to_multiple(size, sizeof(uint64_t));

    /* Check for overflow */
    if (r < size)
        return SIZE_MAX;

    return r;
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    const size_t padding_size = _get_padding_size(alignment);
    const size_t block_size = _calculate_block_size(alignment, size);
    malloc_header_t* header = NULL;
    size_t rsize = _round_up_to_multiple(size, sizeof(uint64_t));
    uint8_t* ptr = NULL;

    if (memptr)
        *memptr = NULL;

    if (!memptr)
    {
        LIBC_PANIC;
        return EINVAL;
    }

    if (!_is_ptrsize_multiple(alignment) || !_is_pow2(alignment))
    {
        LIBC_PANIC;
        return EINVAL;
    }

    /*
    ** [padding][header][block]
    ** ^                 ^
    ** |                 |
    ** X                 Y
    */

    /* the sum of the parts should add up to total block size */
    if (padding_size + sizeof(malloc_header_t) + rsize != block_size)
    {
        LIBC_PANIC;
        return EINVAL;
    }

    /* check the alignment */
    if ((padding_size + sizeof(malloc_header_t)) % alignment)
    {
        LIBC_PANIC;
        return EINVAL;
    }

    /* allocate the memory */
    if (!(ptr = AllocatePool(block_size)))
    {
        LIBC_PANIC;
        return EINVAL;
    }

    /* resolve pointer to header */
    header = (malloc_header_t*)(ptr + padding_size);

    /* initialize the header */
    header->magic = MALLOC_MAGIC;
    header->size = size;
    header->padding = padding_size;

    *memptr = header + 1;
    return 0;
}
