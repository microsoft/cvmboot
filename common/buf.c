// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buf.h"

#define BUF_CHUNK_SIZE 1024

void buf_release(buf_t* buf)
{
    if (buf && buf->data)
    {
        memset(buf->data, 0xDD, buf->size);
        free(buf->data);
    }

    memset(buf, 0x00, sizeof(buf_t));
}

int buf_clear(buf_t* buf)
{
    if (!buf)
        return -1;

    buf->size = 0;

    return 0;
}

int buf_reserve(buf_t* buf, size_t cap)
{
    if (!buf)
        return -1;

    /* If capacity is bigger than current capacity */
    if (cap > buf->cap)
    {
        void* new_data;
        size_t new_cap;

        /* Double current capacity (will be zero the first time) */
        new_cap = buf->cap * 2;

        /* If capacity still insufficent, round to multiple of chunk size */
        if (cap > new_cap)
        {
            const size_t N = BUF_CHUNK_SIZE;
            new_cap = (cap + N - 1) / N * N;
        }

        /* Expand allocation */
        if (!(new_data = realloc(buf->data, new_cap)))
            return -1;

        buf->data = new_data;
        buf->cap = new_cap;
    }

    return 0;
}

int buf_resize(buf_t* buf, size_t new_size)
{
    if (!buf)
        return -1;

    if (new_size == 0)
    {
        buf_release(buf);
        memset(buf, 0, sizeof(buf_t));
        return 0;
    }

    if (buf_reserve(buf, new_size) != 0)
        return -1;

    if (new_size > buf->size)
        memset(buf->data + buf->size, 0, new_size - buf->size);

    buf->size = new_size;

    return 0;
}

int buf_append(buf_t* buf, const void* data, size_t size)
{
    size_t new_size;

    /* Check arguments */
    if (!buf || !data)
        return -1;

    /* If zero-sized, then success */
    if (size == 0)
        return 0;

    /* Compute the new size */
    new_size = buf->size + size;

    /* If insufficient capacity to hold new data */
    if (new_size > buf->cap)
    {
        int err;

        if ((err = buf_reserve(buf, new_size)) != 0)
            return err;
    }

    /* Copy the data */
    memcpy(buf->data + buf->size, data, size);
    buf->size = new_size;

    return 0;
}

int buf_insert(buf_t* buf, size_t pos, const void* data, size_t size)
{
    int ret = -1;
    size_t rem;

    if (!buf || pos > buf->size)
        goto done;

    if (buf_reserve(buf, buf->size + size) != 0)
        return -1;

    rem = buf->size - pos;

    if (rem)
        memmove(buf->data + pos + size, buf->data + pos, rem);

    if (data)
        memcpy(buf->data + pos, data, size);
    else
        memset(buf->data + pos, 0, size);

    buf->size += size;
    ret = 0;

done:
    return ret;
}

int buf_remove(buf_t* buf, size_t pos, size_t size)
{
    size_t rem;

    if (!buf || pos > buf->size || pos + size > buf->size)
        return -1;

    rem = buf->size - (pos + size);

    if (rem)
        memmove(buf->data + pos, buf->data + pos + size, rem);

    buf->size -= size;

    return 0;
}
