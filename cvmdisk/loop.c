// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <ctype.h>
#include <common/buf.h>
#include <common/exec.h>
#include <common/err.h>
#include <utils/strings.h>
#include <time.h>
#include "loop.h"
#include "eraise.h"
#include "blockdev.h"
#include "globals.h"

int loop_parse(const char* path, uint32_t* loopnum, uint32_t* partnum)
{
    int ret = 0;
    int n;
    uint32_t loopnum_buf = 0;
    uint32_t partnum_buf = 0;

    if (loopnum)
        *loopnum = 0;
    else
        loopnum = &loopnum_buf;

    if (partnum)
        *partnum = 0;
    else
        partnum = &partnum_buf;

    if (!path)
        ERAISE(-EINVAL);

    n = sscanf(path, "/dev/loop%up%u", loopnum, partnum);

    if ((n != 1 && n != 2) || !loopnum)
        ERAISE(-EINVAL);

done:
    return ret;
}

int loop_basename(const char* path, char base[PATH_MAX])
{
    int ret = 0;
    const char* p = path;

    if (strncmp(p, "/dev/loop", 9) != 0)
        ERAISE(-EINVAL);

    p += 9;

    if (!isdigit(*p))
        ERAISE(-EINVAL);

    p++;

    while (isdigit(*p))
        p++;

    strncat(base, path, p - path);

done:
    return ret;
}

void loop_format(char path[PATH_MAX], uint32_t loopnum, uint32_t partnum)
{
    snprintf(path, PATH_MAX, "/dev/loop%up%u", loopnum, partnum);
}

void losetup(const char* disk, char loop[PATH_MAX])
{
    blockdev_t* bd = NULL;
    ssize_t byte_count;
    buf_t buf = BUF_INITIALIZER;
    size_t num_blocks;
    const size_t block_size = 512;
    uint8_t block[block_size];
    uint8_t vhdx_sig[] = { 0x76, 0x68, 0x64, 0x78, 0x66, 0x69, 0x6C, 0x65 };
    uint8_t vhd_sig[] = { 'c', 'o', 'n', 'e', 'c', 't', 'i', 'x' };
    static size_t offset = 0;

    /* open the disk for read */
    if (blockdev_open(disk, O_RDWR | O_EXCL, 0, block_size, &bd) != 0)
        ERR("cannot open disk: %s", disk);

    /* get the size of the disk in bytes */
    if ((byte_count = blockdev_get_size(bd)) < 0)
        ERR("cannot get disk size: %s", disk);

    /* fail if the disk is less than one bock in size */
    if (byte_count < block_size)
        ERR("disk is shorter than blocks size: %zu", block_size);

    /* fail if disk is not a multiple of the block size */
    if ((byte_count % block_size) != 0)
        ERR("disk not a multiple of the %zu", block_size);

    /* calculate the total number of disk blocks */
    num_blocks = byte_count / block_size;

    /* Check the first block (reject VHDX files) */
    {
        if (blockdev_get(bd, 0, block, 1) < 0)
            ERR("failed to read first disk");

        /* if this is a VHDX file */
        if (memcmp(block, vhdx_sig, sizeof(vhdx_sig)) == 0)
        {
            ERR("VHDX disks not supported");
        }
    }

    /* Check the last block */
    {
        if (blockdev_get(bd, num_blocks - 1, block, 1) < 0)
            ERR("failed to read last block of disk");

        /* If the last block is a VHD trailer, then remove from total size */
        if (memcmp(block, vhd_sig, sizeof(vhd_sig)) == 0)
            byte_count -= block_size;
    }

    blockdev_close(bd);

    execf(&buf,
        "losetup -P -o %zu --sizelimit %zu -b 512 -f %s --show --direct-io=on",
        offset, byte_count, disk);

    strcpy(loop, buf_str(&buf));

    buf_release(&buf);
}

void lodetach(const char* loop)
{
    buf_t buf = BUF_INITIALIZER;
    execf(&buf, "losetup -d %s", loop);
    buf_release(&buf);
}
