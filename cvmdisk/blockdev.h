// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_BLOCKDEV_H
#define _CVMBOOT_CVMDISK_BLOCKDEV_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <fcntl.h>
#include "defs.h"

#define BLOCKDEV_DEFAULT_BLOCK_SIZE 512

typedef struct
{
    int fd;
    size_t file_size;
    size_t block_size;
    off_t start; /* starting offset */
    off_t end; /* ending offset */
}
blockdev_t;

ssize_t blockdev_get_size(const blockdev_t* blockdev);

ssize_t blockdev_get(
    blockdev_t* blockdev,
    uint64_t blkno,
    void* blocks,
    size_t count);

ssize_t blockdev_put(
    blockdev_t* blockdev,
    uint64_t blkno,
    const void* blocks,
    size_t count);

int blockdev_open(
    const char* pathname,
    int flags,
    mode_t mode,
    size_t block_size,
    blockdev_t** blockdev);

int blockdev_open_slice(
    const char* pathname,
    int flags,
    mode_t mode,
    size_t block_size,
    off_t start,
    off_t end,
    blockdev_t** blockdev_out);

int blockdev_close(blockdev_t* blockdev);

ssize_t blockdev_getsize64(const char* path);

#if 0
ssize_t blockdev_punch_hole(blockdev_t* blockdev, uint64_t blkno, size_t count);
#endif

#endif /* _CVMBOOT_CVMDISK_BLOCKDEV_H */
