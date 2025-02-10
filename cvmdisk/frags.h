// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_FRAGS_H
#define _CVMBOOT_CVMDISK_FRAGS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#define FRAG_LIST_INITIALIZER { NULL, NULL, 0, 0 }

typedef struct frag
{
    struct frag* next;
    size_t offset;
    size_t length;
}
frag_t;

typedef struct frag_list
{
    struct frag* head;
    struct frag* tail;
    size_t size;
    size_t num_blocks;
}
frag_list_t;

int frags_check_holes(const char* path, size_t offset, size_t end);

int frags_append(frag_list_t* list, size_t offset, size_t length);

void frags_release(frag_list_t* list);

int frags_check(const frag_list_t* list, const char* path, bool zero);

int frags_find(
    const char* path,
    size_t start,
    size_t end,
    frag_list_t* frags,
    frag_list_t* holes);

int frags_copy(
    const frag_list_t* list,
    const char* source,
    size_t source_offset,
    const char* dest,
    size_t dest_offset,
    const char* msg);

int frags_compare(
    const frag_list_t* list,
    ssize_t offset, /* subtracted from disk offset to obtain dest offset */
    const char* disk,
    const char* dest,
    const char* msg);

/* return total size of fragments in bytes */
size_t frags_sizeof(const frag_list_t* list);

void frags_set_bits(
    const frag_list_t* frags,
    uint8_t* bits,
    size_t bits_size);

int frags_load(frag_list_t* frags, size_t* file_size, int fd);

#endif /* _CVMBOOT_CVMDISK_FRAGS_H */
