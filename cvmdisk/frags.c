// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include "frags.h"
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <common/strings.h>
#include "eraise.h"
#include "progress.h"
#include "bits.h"
#include "blockdev.h"
#include "round.h"

#define BLOCK_SIZE 4096

#define MAGIC 0xdead31569c7f4381

typedef struct header
{
    uint64_t magic;
    uint64_t file_size;
    uint64_t list_size;
    uint64_t num_blocks;
}
header_t;

int frags_append(frag_list_t* list, size_t offset, size_t length)
{
    int ret = -1;
    frag_t* frag;
    const size_t block_size = BLOCK_SIZE;

    if (!(frag = calloc(1, sizeof(frag_t))))
        goto done;

    frag->offset = offset;
    frag->length = length;

    if (list->tail)
    {
        list->tail->next = frag;
        list->tail = frag;
    }
    else
    {
        list->head = frag;
        list->tail = frag;
    }

    list->size++;
    list->num_blocks += length / block_size;

    ret = 0;

done:
    return ret;
}

void frags_release(frag_list_t* list)
{
    for (frag_t* p = list->head; p; )
    {
        frag_t* next = p->next;
        free(p);
        p = next;
    }
}

int frags_check(const frag_list_t* list, const char* path, bool zero)
{
    int ret = -1;
    int fd = -1;
    __uint128_t buf[BLOCK_SIZE/sizeof(__uint128_t)];

    if ((fd = open(path, O_RDONLY)) < 0)
        goto done;

    for (const frag_t* p = list->head; p; p = p->next)
    {
        size_t n = p->length / sizeof(buf);

        if (lseek(fd, p->offset, SEEK_SET) != p->offset)
            goto done;

        for (size_t i = 0; i < n; i++)
        {
            if (read(fd, buf, sizeof(buf)) != sizeof(buf))
                goto done;

            if (all_zeros(buf, sizeof(buf)))
            {
                if (!zero)
                    goto done;
            }
            else
            {
                if (zero)
                    goto done;
            }
        }
    }

    ret = 0;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}

int frags_find(
    const char* path,
    size_t start,
    size_t end,
    frag_list_t* frags,
    frag_list_t* holes)
{
    int ret = 0;
    int fd = -1;
    off_t offset = start;
    off_t data = 0;
    off_t hole = 0;
    const size_t buffer_size = BLOCK_SIZE;

    memset(frags, 0, sizeof(frag_list_t));
    memset(holes, 0, sizeof(frag_list_t));

    if ((fd = open(path, O_RDONLY)) < 0)
        ERAISE(-EINVAL);

    /* If file has no holes */
    if (lseek(fd, offset, SEEK_HOLE) < 0)
    {
        ssize_t size;

        if ((size = blockdev_getsize64(path)) < 0)
            ERAISE(-size);

        /* Create a single fragment to contain whole file */
        if (frags_append(frags, 0, size) < 0)
            ERAISE(-EINVAL);

        goto done;
    }

    for (;;)
    {
        /* Find the position of next data fragment */
        {
            data = lseek(fd, offset, SEEK_DATA);

            /* If no more data fragments found */
            if (data < 0 || data >= end)
            {
                if (offset < end)
                {
                    if (frags_append(holes, offset, end - offset) < 0)
                        ERAISE(-EINVAL);
                }
                break;
            }

            if (data > offset)
            {
                if (frags_append(holes, offset, data - offset) < 0)
                    ERAISE(-EINVAL);
            }
        }

        /* Find position of next hole or end of data */
        {
            hole = lseek(fd, data, SEEK_HOLE);

            /* If no more holes found */
            if (hole < 0 || hole >= end)
            {
                if (end > data)
                {
                    if (frags_append(frags, data, end - data) < 0)
                        ERAISE(-EINVAL);
                }
                break;
            }

            if (hole > data)
            {
                if (frags_append(frags, data, hole - data) < 0)
                    ERAISE(-EINVAL);
            }
        }

        offset = hole;
    }

    /* the holes and fragments should add up to size */
    if (frags->num_blocks + holes->num_blocks != (end - start) / buffer_size)
        ERAISE(-EINVAL);

done:

    if (fd >= 0)
        close(fd);

    return ret;
}

int frags_check_holes(const char* path, size_t start, size_t end)
{
    int ret = -1;
    int fd = -1;

    if ((fd = open(path, O_RDONLY)) < 0)
        goto done;

    const off_t n = lseek(fd, start, SEEK_HOLE);

    if (n < 0 || n >= end)
        goto done;

    ret = 0;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}

int frags_copy(
    const frag_list_t* list,
    const char* source,
    size_t source_offset,
    const char* dest,
    size_t dest_offset,
    const char* msg)
{
    int ret = 0;
    int fd1 = -1;
    int fd2 = -1;
    __uint128_t buf[BLOCK_SIZE/sizeof(__uint128_t)];
    size_t j = 0;
    size_t num_blocks = 0;
    progress_t progress;
    size_t fsync_counter = 0;

    if ((fd1 = open(source, O_RDONLY)) < 0)
        ERAISE(-errno);

    if ((fd2 = open(dest, O_RDWR)) < 0)
        ERAISE(-errno);

    /* Calculate number of total blocks */
    for (const frag_t* p = list->head; p; p = p->next)
        num_blocks += p->length / sizeof(buf);

    if (msg)
        progress_start(&progress, msg);

    for (const frag_t* p = list->head; p; p = p->next)
    {
        size_t n = p->length / sizeof(buf);

        for (size_t i = 0; i < n; i++)
        {
            const off_t off1 = p->offset + (i * sizeof(buf));
            const off_t off2 = off1 - source_offset + dest_offset;

            if (pread(fd1, buf, sizeof(buf), off1) != sizeof(buf))
                ERAISE(-errno);

            if (!all_zeros(buf, sizeof(buf)))
            {
                if (pwrite(fd2, buf, sizeof(buf), off2) != sizeof(buf))
                    ERAISE(-errno);

                if ((++fsync_counter % 1024) == 0)
                    fsync(fd2);
            }

            j++;

            if (msg)
                progress_update(&progress, j, num_blocks);
        }
    }

    if (msg)
        progress_end(&progress);

done:

    if (fd1 >= 0)
        close(fd1);

    if (fd2 >= 0)
        close(fd2);

    return ret;
}

int frags_compare(
    const frag_list_t* list,
    ssize_t offset,
    const char* disk,
    const char* dest,
    const char* msg)
{
    int ret = -1;
    int fd1 = -1;
    int fd2 = -1;
    const size_t bufsz = BLOCK_SIZE;
    uint8_t buf1[bufsz];
    uint8_t buf2[bufsz];
    size_t j = 0;
    size_t num_blocks = 0;
    progress_t progress;

    if ((fd1 = open(disk, O_RDONLY)) < 0)
        goto done;

    if ((fd2 = open(dest, O_RDONLY)) < 0)
        goto done;

    /* Calculate number of total blocks */
    for (const frag_t* p = list->head; p; p = p->next)
        num_blocks += p->length / bufsz;

    progress_start(&progress, msg);

    for (const frag_t* p = list->head; p; p = p->next)
    {
        size_t n = p->length / bufsz;

        for (size_t i = 0; i < n; i++)
        {
            const off_t off1 = p->offset + (i * bufsz);
            const off_t off2 = off1 - offset;

            if (pread(fd1, buf1, bufsz, off1) != bufsz)
                goto done;

            if (pread(fd2, buf2, bufsz, off2) != bufsz)
                goto done;

            if (memcmp(buf1, buf2, bufsz) != 0)
                goto done;

            j++;

            /* update progress counter */
            progress_update(&progress, j, num_blocks);
        }
    }

    progress_end(&progress);

    ret = 0;

done:

    if (fd1 >= 0)
        close(fd1);

    if (fd2 >= 0)
        close(fd2);

    return ret;
}

size_t frags_sizeof(const frag_list_t* list)
{
    ssize_t total = 0;

    for (const frag_t* p = list->head; p; p = p->next)
        total += p->length;

    return total;
}

void frags_set_bits(
    const frag_list_t* frags,
    uint8_t* bits,
    size_t bits_size)
{
    for (const frag_t* p = frags->head; p; p = p->next)
    {
        size_t index = p->offset / BLOCK_SIZE;
        size_t count = p->length / BLOCK_SIZE;

        for (size_t i = index; i < index + count; i++)
            set_bit(bits, i);
    }
}

int frags_load(frag_list_t* frags, size_t* file_size, int fd)
{
    int ret = 0;
    uint64_t pair[2];
    header_t header;

    if (!frags || !file_size || fd < 0)
        ERAISE(-EINVAL);

    *file_size = 0;
    memset(frags, 0, sizeof(frag_list_t));

    /* read list header */
    {
        if (read(fd, &header, sizeof(header)) != sizeof(header))
            ERAISE(-errno);

        if (header.magic != MAGIC)
            ERAISE(-EINVAL);

        *file_size = header.file_size;
    }

    /* read list nodes */
    for (size_t i = 0; i < header.list_size; i++)
    {
        if (read(fd, pair, sizeof(pair)) != sizeof(pair))
            ERAISE(-EINVAL);

        ECHECK(frags_append(frags, pair[0], pair[1]));
    }

    if (frags->size != header.list_size)
        ERAISE(-EINVAL);

    if (frags->num_blocks != header.num_blocks)
        ERAISE(-EINVAL);

    /* move file pointer to the next page boundary */
    {
        off_t n;

        /* get offset to th current end of file */
        if ((n = lseek(fd, 0, SEEK_CUR)) < 0)
            ERAISE(-errno);

        /* round to next multiple of the block size */
        off_t r = round_up_to_multiple(n, BLOCK_SIZE);

        /* if not already aligned, then zero pad up to next alignment */
        if (r > n)
        {
            if (lseek(fd, r, SEEK_SET) != r)
                ERAISE(-EINVAL);
        }
    }

done:

    return ret;
}
