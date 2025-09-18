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
#include <stdlib.h>
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
    
    /* Check if this is a dynamic VHD and dump additional info */
    cvmvhd_error_t type_err = CVMVHD_ERROR_INITIALIZER;
    if (cvmvhd_get_type(vhd_file, &type_err) == CVMVHD_TYPE_DYNAMIC)
    {
        printf("\n=== Dynamic VHD Header ===\n");
        vhd_dynamic_header_t header;
        if (cvmvhd_read_dynamic_header(vhd_file, &header, &type_err) == 0)
        {
            /* Extract big-endian values properly */
            uint64_t data_offset = 0;
            uint64_t table_offset = 0;
            uint32_t header_version = 0;
            
            /* Read big-endian 8-byte values */
            for (int i = 0; i < 8; i++) {
                data_offset = (data_offset << 8) | header.data_offset[i];
                table_offset = (table_offset << 8) | header.table_offset[i];
            }
            
            /* Read big-endian 4-byte header version */
            for (int i = 0; i < 4; i++) {
                header_version = (header_version << 8) | header.header_version[i];
            }
            
            printf("cookie=\"%.8s\"\n", (char*)header.cookie);
            printf("data_offset=%lu\n", data_offset);
            printf("table_offset=%lu\n", table_offset);
            printf("header_version=0x%08x\n", header_version);
            printf("max_table_entries=%u\n", _swapu32(header.max_table_entries));
            printf("block_size=%u\n", _swapu32(header.block_size));
            printf("checksum=0x%08x\n", _swapu32(header.checksum));
            _hexdump("parent_unique_id", header.parent_uuid, sizeof(header.parent_uuid));
            printf("parent_timestamp=%u\n", _swapu32(header.parent_timestamp));
        }
        else
        {
            printf("Warning: Could not read dynamic VHD header: %s\n", type_err.buf);
        }
    }

    ret = 0;

done:

    if (stream)
        fclose(stream);

    return ret;
}

cvmvhd_type_t cvmvhd_get_type(const char* vhd_file, cvmvhd_error_t* err)
{
    cvmvhd_type_t ret = CVMVHD_TYPE_UNKNOWN;
    FILE* stream = NULL;
    vhd_footer_t footer;
    uint8_t first_block[512];
    struct stat statbuf;

    _clear_err(err);

    if (!vhd_file)
    {
        _err(err, "null parameter");
        goto done;
    }

    /* Get file size */
    if (stat(vhd_file, &statbuf) != 0)
    {
        _err(err, "failed to stat: %s", vhd_file);
        goto done;
    }

    /* File must be at least 1KB (512 bytes footer + some data) */
    if (statbuf.st_size < 1024)
    {
        _err(err, "file too small to be a VHD: %s", vhd_file);
        goto done;
    }

    if (!(stream = fopen(vhd_file, "rb")))
    {
        _err(err, "failed to open: %s", vhd_file);
        goto done;
    }

    /* Check if there's a valid footer at the end */
    if (_load_vhd_footer(stream, &footer) < 0)
    {
        _err(err, "no valid VHD footer found: %s", vhd_file);
        goto done;
    }

    /* Read first 512 bytes to check for dynamic VHD signature */
    if (fseek(stream, 0, SEEK_SET) != 0)
    {
        _err(err, "failed to seek to beginning: %s", vhd_file);
        goto done;
    }

    if (fread(first_block, 1, sizeof(first_block), stream) != sizeof(first_block))
    {
        _err(err, "failed to read first block: %s", vhd_file);
        goto done;
    }

    /* Check if first block contains VHD footer signature */
    if (memcmp(first_block, "conectix", 8) == 0)
    {
        /* Footer at beginning indicates dynamic VHD */
        ret = CVMVHD_TYPE_DYNAMIC;
    }
    else
    {
        /* No footer at beginning, but valid footer at end indicates fixed VHD */
        ret = CVMVHD_TYPE_FIXED;
    }

done:

    if (stream)
        fclose(stream);

    return ret;
}

int cvmvhd_read_dynamic_header(const char* vhd_file, vhd_dynamic_header_t* header, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    FILE* stream = NULL;
    cvmvhd_error_t type_err = CVMVHD_ERROR_INITIALIZER;
    
    _clear_err(err);

    if (!vhd_file || !header)
    {
        _err(err, "null parameter");
        goto done;
    }

    /* Verify this is actually a dynamic VHD */
    if (cvmvhd_get_type(vhd_file, &type_err) != CVMVHD_TYPE_DYNAMIC)
    {
        _err(err, "not a dynamic VHD file: %s", vhd_file);
        goto done;
    }

    if (!(stream = fopen(vhd_file, "rb")))
    {
        _err(err, "failed to open: %s", vhd_file);
        goto done;
    }

    /* Dynamic VHD structure: 
     * [Footer copy (512 bytes)] + [Dynamic header (1024 bytes)] + [BAT] + [Data blocks]
     * We need to seek to position 512 to read the dynamic header
     */
    if (fseek(stream, 512, SEEK_SET) != 0)
    {
        _err(err, "failed to seek to dynamic header: %s", vhd_file);
        goto done;
    }

    /* Read the dynamic header */
    if (fread(header, 1, sizeof(vhd_dynamic_header_t), stream) != sizeof(vhd_dynamic_header_t))
    {
        _err(err, "failed to read dynamic header: %s", vhd_file);
        goto done;
    }

    /* Verify the dynamic header signature */
    if (memcmp(header->cookie, "cxsparse", 8) != 0)
    {
        _err(err, "invalid dynamic header signature: %s", vhd_file);
        goto done;
    }

    ret = 0;

done:

    if (stream)
        fclose(stream);

    return ret;
}

int cvmvhd_extract_raw_image(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    FILE* vhd_stream = NULL;
    FILE* raw_stream = NULL;
    vhd_dynamic_header_t header;
    vhd_footer_t footer;
    uint32_t* bat = NULL;
    uint8_t* block_buffer = NULL;
    uint64_t table_offset;
    uint32_t max_table_entries;
    uint32_t block_size;
    uint64_t disk_size;

    _clear_err(err);

    if (!vhd_file || !raw_file)
    {
        _err(err, "null parameter");
        goto done;
    }

    /* Verify this is a dynamic VHD and read header */
    if (cvmvhd_read_dynamic_header(vhd_file, &header, err) < 0)
        goto done;

    /* Extract header values with proper endianness */
    table_offset = 0;
    for (int i = 0; i < 8; i++) {
        table_offset = (table_offset << 8) | header.table_offset[i];
    }
    
    max_table_entries = _swapu32(header.max_table_entries);
    block_size = _swapu32(header.block_size);

    /* Get disk size from footer */
    if (!(vhd_stream = fopen(vhd_file, "rb")))
    {
        _err(err, "failed to open VHD file: %s", vhd_file);
        goto done;
    }

    if (_load_vhd_footer(vhd_stream, &footer) < 0)
    {
        _err(err, "failed to load VHD footer: %s", vhd_file);
        goto done;
    }

    disk_size = _swapu64(footer.current_size);

    /* Allocate BAT array */
    bat = malloc(max_table_entries * sizeof(uint32_t));
    if (!bat)
    {
        _err(err, "failed to allocate BAT array");
        goto done;
    }

    /* Read BAT from VHD */
    if (fseek(vhd_stream, table_offset, SEEK_SET) != 0)
    {
        _err(err, "failed to seek to BAT offset");
        goto done;
    }

    if (fread(bat, sizeof(uint32_t), max_table_entries, vhd_stream) != max_table_entries)
    {
        _err(err, "failed to read BAT");
        goto done;
    }

    /* Create raw output file */
    if (!(raw_stream = fopen(raw_file, "wb")))
    {
        _err(err, "failed to create raw file: %s", raw_file);
        goto done;
    }

    /* Allocate block buffer */
    block_buffer = malloc(block_size);
    if (!block_buffer)
    {
        _err(err, "failed to allocate block buffer");
        goto done;
    }

    /* Extract blocks */
    for (uint32_t i = 0; i < max_table_entries; i++)
    {
        uint32_t block_offset = _swapu32(bat[i]);
        
        if (block_offset == 0xFFFFFFFF)
        {
            /* Unallocated block - write zeros */
            memset(block_buffer, 0, block_size);
        }
        else
        {
            /* Allocated block - read from VHD */
            uint64_t file_offset = (uint64_t)block_offset * 512;
            
            /* Skip block bitmap - bitmap size is (block_size / 512) bits, rounded up to sector boundary */
            uint32_t sectors_per_block = block_size / 512;
            uint32_t bitmap_size_bits = sectors_per_block;
            uint32_t bitmap_size_bytes = (bitmap_size_bits + 7) / 8;  /* Round up to bytes */
            uint32_t bitmap_size_sectors = (bitmap_size_bytes + 511) / 512;  /* Round up to sectors */
            uint64_t data_offset = file_offset + (bitmap_size_sectors * 512);
            
            if (fseek(vhd_stream, data_offset, SEEK_SET) != 0)
            {
                _err(err, "failed to seek to block %u data at offset %lu", i, data_offset);
                goto done;
            }
            
            if (fread(block_buffer, 1, block_size, vhd_stream) != block_size)
            {
                _err(err, "failed to read block %u", i);
                goto done;
            }
        }

        /* Write block to raw file */
        if (fwrite(block_buffer, 1, block_size, raw_stream) != block_size)
        {
            _err(err, "failed to write block %u to raw file", i);
            goto done;
        }

        /* Check if we've written enough data */
        if ((uint64_t)(i + 1) * block_size >= disk_size)
            break;
    }

    printf("Extracted %lu bytes from dynamic VHD to raw image\n", disk_size);
    ret = 0;

done:
    if (vhd_stream)
        fclose(vhd_stream);
    if (raw_stream)
        fclose(raw_stream);
    if (bat)
        free(bat);
    if (block_buffer)
        free(block_buffer);

    return ret;
}
