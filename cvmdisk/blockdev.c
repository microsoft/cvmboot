// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "blockdev.h"
#include "eraise.h"

/*
**==============================================================================
**
** local definitions:
**
**==============================================================================
*/

static bool _is_power_of_two(size_t x)
{
    for (size_t i = 0; i < sizeof(size_t) * 8; i++)
    {
        if (x == ((size_t)1 << i))
            return true;
    }

    return false;
}

static ssize_t _get_file_size(int fd)
{
    ssize_t ret = 0;
    struct stat st;
    size_t size;

    if (fstat(fd, &st) != 0)
        ERAISE(-errno);

    if (S_ISREG(st.st_mode))
        size = st.st_size;
    else if (ioctl(fd, BLKGETSIZE64, &size) != 0)
        ERAISE(-errno);

    ret = size;

done:
    return ret;
}

static ssize_t _read_blocks(
    int fd, void* data, size_t block_size, size_t count)
{
    ssize_t ret = 0;
    uint8_t* ptr = (uint8_t*)data;
    size_t rem = block_size * count;
    size_t blocks_read = 0;

    while (rem > 0)
    {
        ssize_t n;

        if ((n = read(fd, ptr, block_size)) < 0)
            ERAISE(-errno);

        if (n != (ssize_t)block_size)
            ERAISE(-EIO);

        ptr += n;
        rem -= n;
        blocks_read++;
    }

    ret = blocks_read;

done:

    return ret;
}

static ssize_t _write_blocks(
    int fd, const void* data, size_t block_size, size_t count)
{
    ssize_t ret = 0;
    const uint8_t* ptr = (uint8_t*)data;
    size_t rem = block_size * count;
    size_t blocks_written = 0;

    while (rem > 0)
    {
        ssize_t n;

        if ((n = write(fd, ptr, block_size)) < 0)
            ERAISE(-errno);

        if (n != (ssize_t)block_size)
            ERAISE(-EIO);

        ptr += n;
        rem -= n;
        blocks_written++;
    }

    ret = blocks_written;

done:

    return ret;
}

static int _blockdev_seek(blockdev_t* blockdev, off_t offset)
{
    off_t ret = 0;
    const off_t off = blockdev->start + offset;

    if (offset >= blockdev->end)
        ERAISE(-ERANGE);

    if (lseek(blockdev->fd, off, SEEK_SET) != off)
        ERAISE(-errno);

done:
    return ret;
}

/*
**==============================================================================
**
** blockdev:
**
**==============================================================================
*/

ssize_t blockdev_get_size(const blockdev_t* blockdev)
{
    return blockdev->file_size;
}

ssize_t blockdev_get(
    blockdev_t* blockdev,
    uint64_t blkno,
    void* blocks,
    size_t count)
{
    ssize_t ret = 0;
    off_t offset;
    size_t total_bytes;
    ssize_t n;

    if (!blocks || count == 0)
        ERAISE(-EINVAL);

    offset = blkno * blockdev->block_size;
    total_bytes = count * blockdev->block_size;

    if (offset + total_bytes  > blockdev->file_size)
        ERAISE(-ERANGE);

    ERAISE(_blockdev_seek(blockdev, offset));

    if ((n = _read_blocks(
        blockdev->fd, blocks, blockdev->block_size, count)) != (ssize_t)count)
    {
        ERAISE(n);
    }

done:
    return ret;
}

ssize_t blockdev_put(
    blockdev_t* blockdev,
    uint64_t blkno,
    const void* blocks,
    size_t count)
{
    ssize_t ret = 0;
    off_t offset;
    size_t total_bytes;
    ssize_t n;

    if (!blocks || count == 0)
        ERAISE(-EINVAL);

    offset = (blkno * blockdev->block_size);

    ERAISE(_blockdev_seek(blockdev, offset));

    total_bytes = blockdev->block_size * count;

    if ((n = _write_blocks(
        blockdev->fd, blocks, blockdev->block_size, count)) != (ssize_t)count)
    {
        ERAISE(n);
    }

    if (offset + total_bytes > blockdev->file_size)
        blockdev->file_size += total_bytes;

done:
    return ret;
}

int blockdev_open(
    const char* pathname,
    int flags,
    mode_t mode,
    size_t block_size,
    blockdev_t** blockdev_out)
{
    int ret = 0;
    int fd = -1;
    blockdev_t* blockdev = NULL;
    size_t file_size;

    if (blockdev_out)
        *blockdev_out = NULL;

    // Check for bad parameters.
    if (!pathname)
        ERAISE(-EINVAL);

    // The block_size must be non-zero and a power of two.
    if (block_size == 0 || !_is_power_of_two(block_size))
        ERAISE(-EINVAL);

    // Open the file.
    if ((fd = open(pathname, flags, mode)) < 0)
        ERAISE(-errno);

    // Get the size of the file referred to by fd.
    if ((file_size = _get_file_size(fd)) < 0)
        ERAISE(file_size);

    // Fail if the file size is not a multiple of the block size.
    if ((file_size % block_size) != 0)
        ERAISE(-ERANGE);

    // Create the new block device.
    {
        if (!(blockdev = malloc(sizeof(blockdev_t))))
            ERAISE(-EINVAL);

        blockdev->fd = fd;
        blockdev->file_size = file_size;
        blockdev->block_size = block_size;
        blockdev->start = 0;
        blockdev->end = file_size;
    }

    *blockdev_out = blockdev;
    blockdev = NULL;
    fd = -1;

done:

    if (blockdev)
        free(blockdev);

    if (fd >= 0)
        close(fd);

    return ret;
}

int blockdev_open_slice(
    const char* pathname,
    int flags,
    mode_t mode,
    size_t block_size,
    off_t start,
    off_t end,
    blockdev_t** blockdev_out)
{
    int ret = 0;
    int fd = -1;
    blockdev_t* blockdev = NULL;
    size_t file_size;

    if (blockdev_out)
        *blockdev_out = NULL;

    if (!pathname)
        ERAISE(-EINVAL);

    if (start % block_size)
        ERAISE(-EINVAL);

    if (end % block_size)
        ERAISE(-EINVAL);

    if (start >= end)
        ERAISE(-EINVAL);

    // The block_size must be non-zero and a power of two.
    if (block_size == 0 || !_is_power_of_two(block_size))
        ERAISE(-EINVAL);

    // Open the file.
    if ((fd = open(pathname, flags, mode)) < 0)
        ERAISE(-errno);

    // Get the size of the file referred to by fd.
    if ((file_size = _get_file_size(fd)) < 0)
        ERAISE(file_size);

    if (start >= file_size)
        ERAISE(-EINVAL);

    if (end >= file_size)
        ERAISE(-EINVAL);

    /* Adjust file size to size of slice */
    file_size = end - start;

    // Fail if the file size is not a multiple of the block size.
    if ((file_size % block_size) != 0)
        ERAISE(-ERANGE);

    // Create the new block device.
    {
        if (!(blockdev = malloc(sizeof(blockdev_t))))
            ERAISE(-EINVAL);

        blockdev->fd = fd;
        blockdev->file_size = file_size;
        blockdev->block_size = block_size;
        blockdev->start = start;
        blockdev->end = end;
    }

    *blockdev_out = blockdev;
    blockdev = NULL;
    fd = -1;

done:

    if (blockdev)
        free(blockdev);

    if (fd >= 0)
        close(fd);

    return ret;
}

int blockdev_close(blockdev_t* blockdev)
{
    int ret = 0;
    int r;

    if (!blockdev)
        ERAISE(-EINVAL);

    if ((r = close(blockdev->fd)) < 0)
        ERAISE(-errno);

    free(blockdev);

done:
    return ret;
}

#if 0
ssize_t blockdev_punch_hole(blockdev_t* blockdev, uint64_t blkno, size_t count)
{
    ssize_t ret = 0;
    off_t offset;
    size_t len;

    if (!blockdev)
        ERAISE(-EINVAL);

    offset = (blkno * blockdev->block_size);
    len = blockdev->block_size * count;

    if (fallocate(blockdev->fd, FALLOC_FL_PUNCH_HOLE, offset, len) < 0)
        ERAISE(-errno);

done:
    return ret;
}
#endif

ssize_t blockdev_getsize64(const char* path)
{
    ssize_t ret = 0;
    struct stat st;
    size_t size;
    int fd = -1;

    if (stat(path, &st) != 0)
        ERAISE(-errno);

    if (S_ISREG(st.st_mode))
        size = st.st_size;
    else
    {
        if ((fd = open(path, O_RDONLY)) < 0)
            ERAISE(-errno);

        if (ioctl(fd, BLKGETSIZE64, &size) != 0)
            ERAISE(-errno);
    }

    ret = size;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}
