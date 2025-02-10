// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

_Static_assert(BUFSIZ >= BUFFER_SIZE);

int sparsecmp(const char* file1, const char* file2)
{
    int ret = -1;
    int fd1 = -1;
    int fd2 = -1;
    off_t data1 = 0;
    off_t data2 = 0;
    off_t hole1 = 0;
    off_t hole2 = 0;
    off_t offset = 0;
    size_t total_size = 0;
    char buf1[BUFFER_SIZE];
    char buf2[BUFFER_SIZE];

    if ((fd1 = open(file1, O_RDONLY)) < 0)
    {
        fprintf(stderr, "failed to open %s\n", file1);
        goto done;
    }

    if ((fd2 = open(file2, O_RDONLY)) < 0)
    {
        fprintf(stderr, "failed to open %s\n", file2);
        goto done;
    }

    for (;;)
    {
        /* find position of next data */
        {
            data1 = lseek(fd1, offset, SEEK_DATA);
            data2 = lseek(fd2, offset, SEEK_DATA);

            if (data1 != data2)
            {
                fprintf(stderr, "mismatch data lseek\n");
                goto done;
            }

            if (data1 < 0)
            {
                /* end of file */
                break;
            }
        }

        /* Find position of next hole or end-of-file */
        {
            hole1 = lseek(fd1, data1, SEEK_HOLE);
            hole2 = lseek(fd2, data2, SEEK_HOLE);

            if (hole1 != hole2)
            {
                fprintf(stderr, "mismatch hole lseek\n");
                goto done;
            }

            if (hole1 < 0)
            {
                hole1 = lseek(fd1, data1, SEEK_END);
                hole2 = lseek(fd2, data2, SEEK_END);

                if (hole1 != hole2)
                {
                    fprintf(stderr, "mismatch end seek\n");
                    goto done;
                }

                if (hole1 < 0)
                {
                    fprintf(stderr, "seek EOF failed\n");
                    goto done;
                }
            }
        }

        //printf("frag %zu:%zu: size=%zu\n", data1, hole1, hole1 - data1);

        size_t size = hole1 - data1;

        if (size == 0)
        {
            fprintf(stderr, "unexpected zero size fragment\n");
            goto done;
        }

        size_t blocks = size / BUFFER_SIZE;
        size_t rem = size % BUFFER_SIZE;

        for (size_t i = 0; i < blocks; i++)
        {
            if (read(fd1, buf1, sizeof(buf1)) != BUFFER_SIZE)
            {
                fprintf(stderr, "read error: buf1\n");
                goto done;
            }

            if (read(fd2, buf2, sizeof(buf2)) != BUFFER_SIZE)
            {
                fprintf(stderr, "read error: buf2\n");
                goto done;
            }

            if (memcmp(buf1, buf2, BUFFER_SIZE) != 0)
            {
                fprintf(stderr, "data mismatch\n");
                goto done;
            }
        }

        if (rem > 0)
        {
            if (read(fd1, buf1, sizeof(buf1)) != rem)
            {
                fprintf(stderr, "read error: rem1\n");
                goto done;
            }

            if (read(fd2, buf2, sizeof(buf2)) != rem)
            {
                fprintf(stderr, "read error: rem2\n");
                goto done;
            }

            if (memcmp(buf1, buf2, rem) != 0)
            {
                fprintf(stderr, "data mismatch\n");
                goto done;
            }
        }

        total_size += hole1 - data1;
        offset = hole1;
    }

    //printf("total_size=%zu\n", total_size);

    ret = 0;

done:

    if (fd1 >= 0)
        close(fd1);

    if (fd2 >= 0)
        close(fd2);

    return ret;
}
