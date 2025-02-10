// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include "sparse.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <fcntl.h>
#include <utils/strings.h>
#include <common/strings.h>
#include <utils/sha256.h>
#include "eraise.h"
#include "frags.h"
#include "blockdev.h"
#include "progress.h"
#include "shasha256.h"

#define BLOCK_SIZE 4096

int sparse_copy(const char* source, const char* dest)
{
    int ret = 0;
    int fd = -1;
    int fd1 = -1;
    int fd2 = -1;
    size_t size;
    size_t extra;
    uint8_t zeros[BLOCK_SIZE];
    frag_list_t frags = FRAG_LIST_INITIALIZER;
    frag_list_t holes = FRAG_LIST_INITIALIZER;
    char msg[2*PATH_MAX + 64];

    memset(zeros, 0, sizeof(zeros));

    size = blockdev_getsize64(source);

    /* Reduce size to be multiple of the block size */
    extra = size % BLOCK_SIZE;
    size -= extra;

    /* source file must have at least one block */
    if (size < BLOCK_SIZE)
        ERAISE(-EINVAL);

    /* source file size must be a multiple of the block size */
    if (size % BLOCK_SIZE)
        ERAISE(-EINVAL);

    /* Create or truncate file */
    if ((fd = open(dest, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0)
        ERAISE(-errno);

    /* Seek to last block */
    if (lseek(fd, size - BLOCK_SIZE, SEEK_SET) != size - BLOCK_SIZE)
        ERAISE(-errno);

    /* Write final block (all blocks before will be sparse) */
    if (write(fd, zeros, sizeof(zeros)) != sizeof(zeros))
        ERAISE(-errno);

    /* Close the file */
    close(fd);
    fd = -1;

    /* Find the non-sparse data fragments */
    if (frags_find(source, 0, size, &frags, &holes) < 0)
        ERAISE(-EINVAL);

    snprintf(msg, sizeof(msg), "Copying %s => %s", source, dest);

    /* Perform the copy */
    if (frags_copy(&frags, source, 0, dest, 0, msg) < 0)
        ERAISE(-EINVAL);

    /* Copy extra data (partial block) */
    if (extra > 0)
    {
        char buf[BLOCK_SIZE];

        /* Open source file */
        if ((fd1 = open(source, O_RDONLY, 0644)) < 0)
            ERAISE(-errno);

        /* Open dest file */
        if ((fd2 = open(dest, O_RDWR, 0644)) < 0)
            ERAISE(-errno);

        /* Read extra partial block from end of source file */
        if (pread(fd1, buf, extra, size) != extra)
            ERAISE(-errno);

        /* Write extra partial block to end of dest file */
        if (pwrite(fd2, buf, extra, size) != extra)
            ERAISE(-errno);
    }

done:

    frags_release(&frags);
    frags_release(&holes);

    if (fd >= 0)
        close(fd);

    if (fd1 >= 0)
        close(fd1);

    if (fd2 >= 0)
        close(fd2);

    return ret;
}

int sparse_cat(const char* dest)
{
    int ret = 0;
    int fd;
    ssize_t n;
    void* buf;
    size_t buf_size = 1024 * 1024;
    off_t off = 0;

    if (!dest)
        ERAISE(-EINVAL);

    if (!(buf = malloc(buf_size)))
        ERAISE(-ENOMEM);

    /* Create or truncate file */
    if ((fd = open(dest, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0)
        ERAISE(-errno);

    while ((n = read(STDIN_FILENO, buf, buf_size)) > 0)
    {
        if (n > 65536)
            printf("n=%zu\n", n);

        if (all_zeros(buf, n))
        {
            /* leave potential hole */
            if (lseek(fd, n, SEEK_CUR) < 0)
                ERAISE(-errno);
        }
        else
        {
            /* write data */
            if (write(fd, buf, n) != n)
                ERAISE(-errno);
        }

        off += n;
    }

    /* rewrite final byte of file to extend to full size */
    if (off >= 1)
    {
        uint8_t c;

        if (lseek(fd, off-1, SEEK_SET) < 0)
            ERAISE(-errno);

        if (read(fd, &c, sizeof(c)) != 1)
            ERAISE(-errno);

        if (lseek(fd, off-1, SEEK_SET) < 0)
            ERAISE(-errno);

        if (write(fd, &c, sizeof(c)) != 1)
            ERAISE(-errno);
    }

done:
    return ret;
}

static ssize_t _readn_shasha(int fd, off_t off, size_t len, bool is_hole, shasha256_ctx_t* ctx)
{
    ssize_t ret = 0;
    const size_t bufsz = 64*1024;
    char buf[bufsz];
    char zeros[bufsz];
    size_t r = len;
    ssize_t nread = 0;

    memset(zeros, 0, sizeof(zeros));

    while (r)
    {
        const size_t count = (r < sizeof(buf)) ? r : sizeof(buf);
        ssize_t n;
        void* ptr;

        if (is_hole)
        {
            ptr = zeros;
            n = count;
        }
        else
        {
            ptr = buf;
            n = pread(fd, buf, count, off);
        }

        if (n > 0)
        {
            shasha256_update(ctx, ptr, n, ptr == zeros);
            r -= n;
            off += n;
            nread += n;
        }
        else if (n == 0)
        {
            ret = nread;
            goto done;
        }
        else
        {
            ret = -errno;
            goto done;
        }
    }

    ret = nread;

done:
    return ret;
}

int sparse_shasha256(const char* path, sha256_t* hash)
{
    int ret = 0;
    int fd = -1;
    off_t offset = 0;
    off_t end = 0;
    off_t data = 0;
    off_t hole = 0;
    shasha256_ctx_t ctx;

    shasha256_init(&ctx);

    if ((fd = open(path, O_RDONLY)) < 0)
        ERAISE(-EINVAL);

    /* Get size of file */
    {
        struct stat statbuf;

        if (fstat(fd, &statbuf) < 0)
            ERAISE(-errno);

        end = statbuf.st_size;
    }

    /* If file has no holes */
    if (lseek(fd, offset, SEEK_HOLE) < 0)
    {
        ssize_t size;

        if ((size = blockdev_getsize64(path)) < 0)
            ERAISE(-size);

        if (_readn_shasha(fd, offset, size, false, &ctx) != size)
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
                    const size_t len = end - offset;

                    if (_readn_shasha(fd, offset, len, true, &ctx) != len)
                        ERAISE(-EINVAL);
                }
                break;
            }

            if (data > offset)
            {
                const size_t len = data - offset;

                if (_readn_shasha(fd, offset, len, true, &ctx) != len)
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
                    const size_t len = end - data;

                    if (_readn_shasha(fd, data, len, false, &ctx) != len)
                        ERAISE(-EINVAL);
                }
                break;
            }

            if (hole > data)
            {
                const size_t len = hole - data;

                if (_readn_shasha(fd, data, len, false, &ctx) != len)
                    ERAISE(-EINVAL);
            }
        }

        offset = hole;
    }

    shasha256_final(hash, &ctx);

done:

    if (fd >= 0)
        close(fd);

    return ret;
}
