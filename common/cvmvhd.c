// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <stdarg.h>
#include <errno.h>
#include "cvmvhd.h"

/*
**==============================================================================
**
** Local definitions:
**
**==============================================================================
*/

#define SECTOR_SIZE 512

__attribute__((format(printf, 2, 3)))
static void _err(cvmvhd_error_t* err, char* fmt, ...)
{
    if (err)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->buf, sizeof(err->buf), fmt, ap);
        va_end(ap);
    }
}

static void _clear_err(cvmvhd_error_t* err)
{
    if (err)
        *err->buf = '\0';
}

static uint64_t _swapu64(uint64_t x)
{
    return
        ((uint64_t)((x & 0xFF) << 56)) |
        ((uint64_t)((x & 0xFF00) << 40)) |
        ((uint64_t)((x & 0xFF0000) << 24)) |
        ((uint64_t)((x & 0xFF000000) << 8)) |
        ((uint64_t)((x & 0xFF00000000) >> 8)) |
        ((uint64_t)((x & 0xFF0000000000) >> 24)) |
        ((uint64_t)((x & 0xFF000000000000) >> 40)) |
        ((uint64_t)((x & 0xFF00000000000000) >> 56));
}

static uint32_t _swapu32(uint32_t x)
{
    return
        ((uint32_t)((x & 0x000000FF) << 24)) |
        ((uint32_t)((x & 0x0000FF00) << 8)) |
        ((uint32_t)((x & 0x00FF0000) >> 8)) |
        ((uint32_t)((x & 0xFF000000) >> 24));
}

static uint16_t _swapu16(uint16_t x)
{
    return
        ((uint16_t)((x & 0x00FF) << 8)) |
        ((uint16_t)((x & 0xFF00) >> 8));

}

static void _hexdump(const char* header, const void* data, size_t size)
{
    const uint8_t* p = data;

    printf("%s=", header);

    for (size_t i = 0; i < size; i++)
        printf("%02x", p[i]);

    printf("\n");
}

void _compute_disk_geometry(
    uint64_t totalSectors,
    disk_geometry_t* disk_geometry)
{
    uint64_t sectorsPerTrack;
    uint64_t heads;
    uint64_t cylinderTimesHeads;
    uint64_t cylinders;

    if (totalSectors > 65535 * 16 * 255)
    {
       totalSectors = 65535 * 16 * 255;
    }

    if (totalSectors >= 65535 * 16 * 63)
    {
       sectorsPerTrack = 255;
       heads = 16;
       cylinderTimesHeads = totalSectors / sectorsPerTrack;
    }
    else
    {
       sectorsPerTrack = 17;
       cylinderTimesHeads = totalSectors / sectorsPerTrack;

       heads = (cylinderTimesHeads + 1023) / 1024;

       if (heads < 4)
       {
          heads = 4;
       }
       if (cylinderTimesHeads >= (heads * 1024) || heads > 16)
       {
          sectorsPerTrack = 31;
          heads = 16;
          cylinderTimesHeads = totalSectors / sectorsPerTrack;
       }
       if (cylinderTimesHeads >= (heads * 1024))
       {
          sectorsPerTrack = 63;
          heads = 16;
          cylinderTimesHeads = totalSectors / sectorsPerTrack;
       }
    }

    cylinders = cylinderTimesHeads / heads;
    disk_geometry->cylinders = cylinders;
    disk_geometry->heads = heads;
    disk_geometry->sectors = sectorsPerTrack;
}

uint32_t _compute_checksum(const vhd_footer_t* footer)
{
    vhd_footer_t f = *footer;
    f.checksum = 0;
    const uint8_t* p = (const uint8_t*)&f;
    size_t n = sizeof(f);
    uint32_t sum = 0;

    while (n--)
        sum += *p++;

    return ~sum;
}

static void _dump_vhd_footer(const vhd_footer_t* p)
{
    printf("cookie=\"%.8s\"\n", (const char*)p->cookie);
    _hexdump("features", p->features, sizeof(p->features));
    _hexdump("format_version", p->format_version, sizeof(p->format_version));
    _hexdump("data_offset", p->data_offset, sizeof(p->data_offset));
    printf("timestamp=%u\n", _swapu32(p->timestamp));
    printf("creator_application=\"%.4s\"\n",
        (const char*)p->creator_application);
    _hexdump("creator_version",
        p->creator_version, sizeof(p->creator_version));
    printf("creator_host_os=\"%.4s\"\n", (const char*)p->creator_host_os);
    printf("original_size=%lu\n", _swapu64(p->original_size));
    printf("current_size=%lu\n", _swapu64(p->current_size));
    printf("disk_geometry.cylinders=%u\n",
        _swapu16(p->disk_geometry.cylinders));
    printf("disk_geometry.heads=%u\n", p->disk_geometry.heads);
    printf("disk_geometry.sectors=%u\n", p->disk_geometry.sectors);
    printf("disk_type=%u\n", _swapu32(p->disk_type));
    printf("checksum=%u\n", _swapu32(p->checksum));
    _hexdump("unique_id", p->unique_id, sizeof(p->unique_id));
    _hexdump("saved_state", &p->saved_state, sizeof(p->saved_state));
    _hexdump("reserved", p->reserved, sizeof(p->reserved));
}

static int _load_vhd_footer(FILE* stream, vhd_footer_t* footer)
{
    memset(footer, 0, sizeof(vhd_footer_t));

    if (fseek(stream, -sizeof(vhd_footer_t), SEEK_END) != 0)
        return -1;

    if (fread(footer, 1, sizeof(vhd_footer_t), stream) != sizeof(vhd_footer_t))
        return -1;

    if (memcmp(footer->cookie, "conectix", sizeof(footer->cookie)) != 0)
        return -1;

    return 0;
}

static const vhd_footer_t _footer_template =
{
    .cookie= {'c','o','n','e','c','t','i','x'},
    .features={0x00,0x00,0x00,0x02},
    .format_version={0x00,0x01,0x00,0x00},
    .data_offset={0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
    .timestamp=785281372,
    .creator_application= {'w','i','n',' '},
    .creator_version={0x00,0x0a,0x00,0x00},
    .creator_host_os= {'W','i','2','k'},
    .original_size=0, /* required */
    .current_size=0, /* required */
    .disk_geometry.cylinders=0, /* required */
    .disk_geometry.heads=0, /* required */
    .disk_geometry.sectors=0, /* required */
    .disk_type=2, /* required */
    .checksum=0, /* required */
    .unique_id={0x00}, /* required */
    .saved_state=0x00, /* required */
};

static void _init_footer(vhd_footer_t* footer, size_t size)
{
    vhd_footer_t f = _footer_template;

    f.original_size = _swapu64(size);
    f.current_size = _swapu64(size);
    _compute_disk_geometry(size / SECTOR_SIZE, &f.disk_geometry);
    f.disk_geometry.cylinders = _swapu16(f.disk_geometry.cylinders);
    f.checksum = _swapu32(_compute_checksum(&f));

    while (getrandom(f.unique_id, UNIQUE_ID_SIZE, 0) != UNIQUE_ID_SIZE)
        ;

    *footer = f;
}

/*
**==============================================================================
**
** Public definitions:
**
**==============================================================================
*/

int cvmvhd_create(const char* vhd_file, size_t size_gb, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    const int one_gb = 1024 * 1024 * 1024; /* one gigabyte */
    const size_t blksz = 4096;
    uint8_t zeros[blksz];
    int fd = -1;

    _clear_err(err);

    if (!vhd_file || !size_gb || !err)
    {
        _err(err, "null parameter");
        goto done;
    }

    memset(zeros, 0, sizeof(zeros));

    /* Create or truncate file */
    {
        int flags = O_RDWR | O_CREAT | O_TRUNC;
        mode_t mode = 0644;

        if ((fd = open(vhd_file, flags, mode)) < 0)
        {
            _err(err, "failed to create file %s", vhd_file);
            goto done;
        }
    }

    /* Seek to last block */
    {
        off_t offset = (size_gb * one_gb) - blksz;

        if (lseek(fd, offset, SEEK_SET) != offset)
        {
            _err(err, "lseek failed: %s", vhd_file);
            goto done;
        }
    }

    /* Write final block (all blocks before will be sparse) */
    if (write(fd, zeros, sizeof(zeros)) != sizeof(zeros))
    {
        _err(err, "failed to write to file: %s", vhd_file);
        goto done;
    }

    if (cvmvhd_append(vhd_file, err) < 0)
        goto done;

    ret = 0;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}

int cvmvhd_resize(const char* vhd_file, size_t size_bytes, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    vhd_footer_t footer;
    vhd_footer_t zeros = { {0} };
    uint64_t old_size;
    uint64_t new_size;
    int fd = -1;

    _clear_err(err);

    if (!vhd_file || !size_bytes || !err)
    {
        _err(err, "null parameter");
        goto done;
    }

    /* Load the footer into memory */
    {
        FILE* stream;

        if (!(stream = fopen(vhd_file, "rb")))
        {
            _err(err, "failed to open: %s", vhd_file);
            goto done;
        }

        if (_load_vhd_footer(stream, &footer) < 0)
        {
            _err(err, "not a VHD file: %s", vhd_file);
            goto done;
        }

        fclose(stream);
    }

    /* Calculate new VHD size and round it up to two megabyte multiple */
    if (size_bytes)
    {
        old_size = _swapu64(footer.current_size);
        new_size = size_bytes;
    }
    else
    {
        _err(err, "unexpected case");
        goto done;
    }

    /* Modify the footer */
    footer.current_size = _swapu64(new_size);
    _compute_disk_geometry(new_size / SECTOR_SIZE, &footer.disk_geometry);
    footer.disk_geometry.cylinders = _swapu16(footer.disk_geometry.cylinders);
    footer.checksum = _swapu32(_compute_checksum(&footer));

    /* Zero out the old footer, expand file, and append new footer */
    {
        /* Open file for append */
        if ((fd = open(vhd_file, O_RDWR|O_APPEND)) < 0)
        {
            _err(err, "failed to open for appending: %s", vhd_file);
            goto done;
        }

        /* Write zeros over the old footer */
        if (lseek(fd, old_size, SEEK_SET) != old_size)
        {
            _err(err, "lseek failed");
            goto done;
        }
        if (write(fd, &zeros, sizeof(zeros)) != sizeof(zeros))
        {
            _err(err, "failed to write the VHD header");
            goto done;
        }

        /* Grow the file to be the new size */
        if (ftruncate(fd, new_size + sizeof(footer)) < 0)
        {
            _err(err, "ftruncate() failed");
            goto done;
        }

        /* Close file to avoid append behavior below */
        close(fd);
        fd = -1;

        /* Open file for read-write */
        if ((fd = open(vhd_file, O_RDWR)) < 0)
        {
            _err(err, "failed to open for appending: %s", vhd_file);
            goto done;
        }

        /* Rewrite the footer (this makes the file 1024 bytes bigger!) */
        if (lseek(fd, new_size, SEEK_SET) != new_size)
        {
            _err(err, "lseek failed on VHD file");
            goto done;
        }
        if (write(fd, &footer, sizeof(footer)) != sizeof(footer))
        {
            _err(err, "failed to write the VHD header");
            goto done;
        }

        /* Close the file */
        close(fd);
        fd = -1;
    }

    /* Check that the file is the expected size */
    {
        struct stat statbuf;
        size_t expect = new_size + sizeof(footer);

        if (stat(vhd_file, &statbuf) != 0)
        {
            _err(err, "failed to stat: %s", vhd_file);
            goto done;
        }

        if (statbuf.st_size != expect)
        {
            _err(err, "file is not expected size: %lu/%zu",
                statbuf.st_size, expect);
            goto done;
        }
    }

    printf("Resized file from %lu to %lu bytes\n", old_size, new_size);

    ret = 0;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}

int cvmvhd_append(const char* vhd_file, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    vhd_footer_t footer;
    size_t image_size;
    int fd = -1;

    _clear_err(err);

    if (!vhd_file || !err)
    {
        _err(err, "null parameter");
        goto done;
    }

    /* Remove existing VHD header if any */
    if (cvmvhd_remove(vhd_file, err) < 0)
        goto done;

    /* Get size of file in bytes */
    {
        struct stat statbuf;

        if (stat(vhd_file, &statbuf) != 0)
        {
            _err(err, "failed to stat: %s", vhd_file);
            goto done;
        }

        image_size = statbuf.st_size;
    }

    /* Initiailize the new footer */
    _init_footer(&footer, image_size);

    /* Append new header */
    {
        if ((fd = open(vhd_file, O_RDWR)) < 0)
        {
            _err(err, "failed to open for appending: %s", vhd_file);
            goto done;
        }

        if (lseek(fd, image_size, SEEK_SET) != image_size)
        {
            _err(err, "lseek failed on VHD file");
            goto done;
        }

        if (write(fd, &footer, sizeof(footer)) != sizeof(footer))
        {
            _err(err, "failed to write the VHD header");
            goto done;
        }

        close(fd);
        fd = -1;
    }

    /* Check that image size grew by footer size */
    {
        struct stat statbuf;

        if (stat(vhd_file, &statbuf) != 0)
        {
            _err(err, "failed to stat: %s", vhd_file);
            goto done;
        }

        if (image_size + sizeof(footer) != statbuf.st_size)
        {
            _err(err, "append failed: %s", vhd_file);
            goto done;
        }
    }

    ret = 0;

done:

    if (fd >= 0)
        close(fd);

    return ret;
}

int cvmvhd_remove(const char* vhd_file, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    FILE* stream = NULL;
    vhd_footer_t footer;
    struct stat statbuf;
    int fd = -1;

    _clear_err(err);

    if (!vhd_file || !err)
    {
        _err(err, "null parameter");
        goto done;
    }

    if (stat(vhd_file, &statbuf) != 0)
    {
        _err(err, "failed to stat: %s", vhd_file);
        goto done;
    }

    if (!(stream = fopen(vhd_file, "rb")))
    {
        _err(err, "failed to open: %s", vhd_file);
        goto done;
    }

    if (_load_vhd_footer(stream, &footer) == 0)
    {
        fclose(stream);
        stream = NULL;

        if ((fd = open(vhd_file, O_RDWR|O_APPEND)) < 0)
        {
            _err(err, "failed to open for appending: %s", vhd_file);
            goto done;
        }

        if (ftruncate(fd, statbuf.st_size - sizeof(vhd_footer_t)) < 0)
        {
            _err(err, "ftruncate() failed");
            goto done;
        }

        close(fd);
        fd = -1;
    }

    ret = 0;

done:

    if (stream)
        fclose(stream);

    if (fd >= 0)
        close(fd);

    return ret;
}

int cvmvhd_dump(const char* vhd_file, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    FILE* stream = NULL;
    vhd_footer_t footer;

    _clear_err(err);

    if (!vhd_file || !err)
    {
        _err(err, "null parameter");
        goto done;
    }

    if (!(stream = fopen(vhd_file, "rb")))
    {
        snprintf(err->buf, sizeof(err->buf), "failed to open: %s", vhd_file);
        goto done;
    }

    if (_load_vhd_footer(stream, &footer) < 0)
    {
        snprintf(err->buf, sizeof(err->buf), "not a VHD file: %s", vhd_file);
        goto done;
    }

    _dump_vhd_footer(&footer);

    ret = 0;

done:

    if (stream)
        fclose(stream);

    return ret;
}
