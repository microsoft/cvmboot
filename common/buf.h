// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_BUF_H
#define _CVMBOOT_COMMON_BUF_H

#include <stdint.h>

#define BUF_INITIALIZER { NULL, 0, 0, 0 }

typedef struct buf
{
    uint8_t* data;
    size_t size;
    size_t cap;
    size_t offset;
} buf_t;

void buf_release(buf_t* buf);

int buf_clear(buf_t* buf);

int buf_reserve(buf_t* buf, size_t cap);

int buf_resize(buf_t* buf, size_t new_size);

int buf_append(buf_t* buf, const void* p, size_t size);

int buf_insert(buf_t* buf, size_t pos, const void* data, size_t size);

int buf_remove(buf_t* buf, size_t pos, size_t size);

static inline const char* buf_str(const buf_t* buf)
{
    return (const char*)buf->data;
}

#endif /* _CVMBOOT_COMMON_BUF_H */
