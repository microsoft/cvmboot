// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_STRARR_H
#define _CVMBOOT_COMMON_STRARR_H

#include <stddef.h>
#include <string.h>

#define STRARR_INITIALIZER { NULL, 0, 0 }

typedef struct strarr
{
    char** data;
    size_t size;
    size_t capacity;
} strarr_t;

static inline void strarr_init(strarr_t* self)
{
    memset(self, 0, sizeof(strarr_t));
}

void strarr_release(strarr_t* self);

int strarr_append(strarr_t* self, const char* data);

int strarr_remove(strarr_t* self, size_t index);

void strarr_sort(strarr_t* self);

#endif /* _CVMBOOT_COMMON_STRARR_H */
