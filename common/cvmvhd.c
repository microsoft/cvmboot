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
#include "exec.h"
#include "buf.h"

/*
**==============================================================================
**
** Local definitions:
**
**==============================================================================
*/


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

/* Forward declarations for static helper functions */
static int _extract_dynamic_vhd(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err);
static int _extract_fixed_vhd(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err);

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
    .disk_type=0x02000000, /* required - big-endian format (2 as uint32_t BE) */
    .checksum=0, /* required */
    .unique_id={0x00}, /* required */
    .saved_state=0x00, /* required */
};

static void _init_footer(vhd_footer_t* footer, size_t size)
{
    vhd_footer_t f = _footer_template;

    f.original_size = _swapu64(size);
    f.current_size = _swapu64(size);
    _compute_disk_geometry(size / VHD_SECTOR_SIZE, &f.disk_geometry);
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

    /* Validate that this is a fixed VHD - resize only works for fixed VHDs */
    {
        cvmvhd_type_t vhd_type;
        cvmvhd_error_t type_err = CVMVHD_ERROR_INITIALIZER;
        
        vhd_type = cvmvhd_get_type(vhd_file, &type_err);
        if (vhd_type == CVMVHD_TYPE_UNKNOWN)
        {
            _err(err, "failed to determine VHD type: %s", type_err.buf);
            goto done;
        }
        
        if (vhd_type != CVMVHD_TYPE_FIXED)
        {
            _err(err, "resize operation only supports fixed VHDs (detected: %s VHD). Use expand/compact commands for dynamic VHD conversion.", 
                 vhd_type == CVMVHD_TYPE_DYNAMIC ? "dynamic" : "unknown");
            goto done;
        }
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
    _compute_disk_geometry(new_size / VHD_SECTOR_SIZE, &footer.disk_geometry);
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

/* Determine VHD file type (fixed vs. dynamic) based on file structure analysis
 * 
 * Analyzes a VHD file to determine if it's a fixed or dynamic VHD by examining
 * the presence and location of VHD footers according to the VHD specification.
 * 
 * VHD Type Detection Algorithm (per Microsoft VHD Specification v1.0):
 * - All VHD files have a 512-byte footer at the end with "conectix" signature
 * - Fixed VHDs: Only have footer at the end, data starts immediately
 * - Dynamic VHDs: Have footer copy at beginning AND footer at end
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Hard Disk Footer Format
 * 
 * Additional references:
 * - GitHub libvhd VHD format documentation and analysis
 *   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
 * - Microsoft VHD Developer Blog (technical deep-dive)
 *   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
 * 
 * @param vhd_file: Path to VHD file to analyze
 * @param err: Error context for detailed error reporting
 * @return: CVMVHD_TYPE_FIXED, CVMVHD_TYPE_DYNAMIC, or CVMVHD_TYPE_UNKNOWN on error
 */
cvmvhd_type_t cvmvhd_get_type(const char* vhd_file, cvmvhd_error_t* err)
{
    cvmvhd_type_t ret = CVMVHD_TYPE_UNKNOWN;
    FILE* stream = NULL;
    vhd_footer_t footer;
    uint8_t first_block[VHD_SECTOR_SIZE];
    struct stat statbuf;

    _clear_err(err);

    if (!vhd_file)
    {
        _err(err, "Failed to analyze VHD type: null parameter");
        goto done;
    }

    /* Step 1: Verify file size is at least VHD_FOOTER_SIZE (minimum for valid VHD) */
    if (stat(vhd_file, &statbuf) != 0)
    {
        _err(err, "Failed to analyze VHD type: cannot stat file %s", vhd_file);
        goto done;
    }

    if (statbuf.st_size < VHD_FOOTER_SIZE)
    {
        _err(err, "Failed to analyze VHD type: file too small to be a VHD: %s", vhd_file);
        goto done;
    }

    if (!(stream = fopen(vhd_file, "rb")))
    {
        _err(err, "Failed to analyze VHD type: cannot open file %s", vhd_file);
        goto done;
    }

    /* Step 2: Load and validate footer from end of file (required for all VHDs) */
    if (_load_vhd_footer(stream, &footer) < 0)
    {
        _err(err, "Failed to analyze VHD type: no valid VHD footer found in %s", vhd_file);
        goto done;
    }

    /* Step 3: Read first 512 bytes and check for "conectix" signature */
    if (fseek(stream, 0, SEEK_SET) != 0)
    {
        _err(err, "Failed to analyze VHD type: cannot seek to beginning of %s", vhd_file);
        goto done;
    }

    if (fread(first_block, 1, sizeof(first_block), stream) != sizeof(first_block))
    {
        _err(err, "Failed to analyze VHD type: cannot read first block of %s", vhd_file);
        goto done;
    }

    /* Step 4: Determine VHD type based on footer signature location */
    if (memcmp(first_block, "conectix", 8) == 0)
    {
        /* "conectix" found at start → Dynamic VHD (footer copy present) */
        ret = CVMVHD_TYPE_DYNAMIC;
    }
    else
    {
        /* No "conectix" at start but valid footer at end → Fixed VHD */
        ret = CVMVHD_TYPE_FIXED;
    }

done:

    if (stream)
        fclose(stream);

    return ret;
}

/* Read and parse dynamic VHD header structure
 * 
 * Reads the dynamic VHD header from a dynamic VHD file and performs validation
 * of the header signature and structure. The dynamic header contains critical
 * metadata including the Block Allocation Table (BAT) location and block size.
 * 
 * Dynamic VHD Structure (per VHD Specification v1.0):
 * - Byte 0-511: Footer copy (identical to footer at end)
 * - Byte 512-1535: Dynamic header (1024 bytes) ← This function reads this
 * - Byte 1536+: Block Allocation Table (BAT) starts here
 * 
 * Dynamic Header Contents (all multi-byte values in big-endian):
 * - Bytes 0-7: "cxsparse" cookie signature (required for validation)
 * - Bytes 8-15: Data offset (unused, set to 0xFFFFFFFF)
 * - Bytes 16-23: Table offset (BAT location in file)
 * - Bytes 24-27: Header version (0x00010000)
 * - Bytes 28-31: Max table entries (number of BAT entries)
 * - Bytes 32-35: Block size (typically 2MB = 2,097,152 bytes)
 * - Bytes 36-39: Checksum (complement sum of header excluding checksum field)
 * - Bytes 40-55: Parent UUID (all zeros for standalone VHDs)
 * - Bytes 56-59: Parent timestamp
 * - Bytes 60-571: Reserved fields
 * - Bytes 572-1023: Parent locator entries (unused for standalone VHDs)
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Dynamic Disk Header Format
 * 
 * Additional references:
 * - GitHub libvhdi VHD format documentation and analysis
 *   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
 * - Microsoft Developer Network VHD Format (archived documentation)
 *   https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd323654(v=vs.85)
 * 
 * @param vhd_file: Path to dynamic VHD file to read from
 * @param header: Pointer to structure that will receive the parsed header data
 * @param err: Error context for detailed error reporting
 * @return: 0 on success, -EINVAL on error
 */
int cvmvhd_read_dynamic_header(const char* vhd_file, vhd_dynamic_header_t* header, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    FILE* stream = NULL;
    cvmvhd_error_t type_err = CVMVHD_ERROR_INITIALIZER;
    
    _clear_err(err);

    if (!vhd_file || !header)
    {
        _err(err, "Failed to read dynamic header: null parameter");
        goto done;
    }

    /* Step 1: Verify this is a dynamic VHD */
    if (cvmvhd_get_type(vhd_file, &type_err) != CVMVHD_TYPE_DYNAMIC)
    {
        _err(err, "Failed to read dynamic header: not a dynamic VHD file: %s", vhd_file);
        goto done;
    }

    if (!(stream = fopen(vhd_file, "rb")))
    {
        _err(err, "failed to open: %s", vhd_file);
        goto done;
    }

    /* Step 2: Seek to dynamic header at offset 512 bytes
     * Dynamic VHD structure: 
     * [Footer copy (512 bytes)] + [Dynamic header (1024 bytes)] + [BAT] + [Data blocks]
     * We need to seek to position 512 to read the dynamic header
     */
    if (fseek(stream, VHD_DYNAMIC_HEADER_OFFSET, SEEK_SET) != 0)
    {
        _err(err, "Failed to read dynamic header: cannot seek to header offset in %s", vhd_file);
        goto done;
    }

    /* Step 3: Read the dynamic header to get BAT location, block size, and metadata */
    if (fread(header, 1, sizeof(vhd_dynamic_header_t), stream) != sizeof(vhd_dynamic_header_t))
    {
        _err(err, "Failed to read dynamic header: incomplete read from %s", vhd_file);
        goto done;
    }

    /* Step 4: Verify the "cxsparse" signature*/
    if (memcmp(header->cookie, "cxsparse", 8) != 0)
    {
        _err(err, "Failed to read dynamic header: invalid signature in %s", vhd_file);
        goto done;
    }

    /* Step 5: Validate header parameters*/
    uint32_t block_size = _swapu32(header->block_size);
    uint32_t max_table_entries = _swapu32(header->max_table_entries);
    
    if (block_size == 0 || block_size > VHD_MAX_BLOCK_SIZE || (block_size % VHD_SECTOR_SIZE) != 0)
    {
        _err(err, "Invalid dynamic VHD: bad block size %u", block_size);
        goto done;
    }
    if (max_table_entries > VHD_MAX_BAT_ENTRIES)
    {
        _err(err, "Invalid dynamic VHD: BAT too large %u entries", max_table_entries);
        goto done;
    }

    ret = 0;

done:

    if (stream)
        fclose(stream);

    return ret;
}

/* Extract raw disk image from VHD file (dispatcher function)
 * 
 * Auto-detects VHD type (fixed vs dynamic) and dispatches to the appropriate
 * extraction helper function. This provides a unified interface for extracting
 * raw disk images from any VHD format.
 * 
 * VHD Type Detection and Dispatch:
 * - Dynamic VHDs: Calls _extract_dynamic_vhd() for complex BAT-based extraction
 * - Fixed VHDs: Calls _extract_fixed_vhd() for simple copy-and-strip operation
 * 
 * Both extraction methods produce identical raw disk images that can be mounted
 * or used directly as block devices
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Block Allocation Table and Data Blocks
 * 
 * Additional references:
 * - GitHub libvhdi VHD format documentation and analysis
 *   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
 * - Microsoft VHD Developer Blog (technical deep-dive)
 *   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
 * 
 * @param vhd_file: Path to source dynamic VHD file
 * @param raw_file: Path for output raw disk image (will be created/overwritten)
 * @param err: Error context for detailed error reporting
 * @return: 0 on success, -EINVAL on error
 */
int cvmvhd_extract_raw_image(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    cvmvhd_type_t vhd_type;
    cvmvhd_error_t type_err;

    _clear_err(err);

    if (!vhd_file || !raw_file)
    {
        _err(err, "Failed to extract raw image: null parameter");
        goto done;
    }

    /* Auto-detect VHD type */
    vhd_type = cvmvhd_get_type(vhd_file, &type_err);
    if (vhd_type == CVMVHD_TYPE_UNKNOWN)
    {
        _err(err, "Failed to extract raw image: cannot determine VHD type: %s", type_err.buf);
        goto done;
    }

    /* Branch based on VHD type */
    if (vhd_type == CVMVHD_TYPE_DYNAMIC)
    {
        ret = _extract_dynamic_vhd(vhd_file, raw_file, err);
    }
    else if (vhd_type == CVMVHD_TYPE_FIXED)
    {
        ret = _extract_fixed_vhd(vhd_file, raw_file, err);
    }
    else
    {
        _err(err, "Failed to extract raw image: unsupported VHD type");
        goto done;
    }

done:
    return ret;
}

/* Extract raw disk image from dynamic VHD (internal helper)
 * 
 * Handles the complex dynamic VHD extraction process including Block Allocation
 * Table (BAT) processing, block reconstruction, and sparse block handling.
 * 
 * Dynamic VHD Extraction Algorithm:
 * 1. Read and validate dynamic VHD header for metadata (BAT location, block size)
 * 2. Extract VHD footer to determine total virtual disk size  
 * 3. Load Block Allocation Table (BAT) to map logical to physical blocks
 * 4. For each logical block in virtual disk:
 *    - If BAT entry is 0xFFFFFFFF: write zeros (unallocated/sparse block)
 *    - If BAT entry is valid offset: read actual block data from VHD file
 * 5. Each allocated block structure: [sector bitmap][block data]
 * 6. Skip bitmap, extract data, write to output raw image file
 * 
 * Technical Implementation Details:
 * - Uses dynamic header to get BAT location and block size (typically 2MB)
 * - BAT entries are 32-bit big-endian values pointing to physical sector offsets
 * - Each data block: 512-byte sector bitmap + N bytes of actual data
 * - Bitmap calculation: (block_size/512) bits rounded up to sector boundary
 * - All multi-byte values converted from big-endian to host endianness
 * - Output raw image is exactly the virtual disk size from VHD footer
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Block Allocation Table and Data Blocks
 * 
 * Additional references:
 * - GitHub libvhdi VHD format documentation and analysis
 *   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
 * - Microsoft VHD Developer Blog (technical deep-dive)
 *   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
 * 
 * @param vhd_file: Path to source dynamic VHD file  
 * @param raw_file: Path for output raw disk image (will be created/overwritten)
 * @param err: Error context for detailed error reporting
 * @return: 0 on success, -EINVAL on error
 */
static int _extract_dynamic_vhd(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err)
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

    /* Step 1: Read and validate dynamic VHD header for metadata (BAT location, block size) */
    if (cvmvhd_read_dynamic_header(vhd_file, &header, err) < 0)
        goto done;

    /* Extract header values with proper endianness */
    table_offset = 0;
    for (int i = 0; i < 8; i++) {
        table_offset = (table_offset << 8) | header.table_offset[i];
    }
    
    max_table_entries = _swapu32(header.max_table_entries);
    block_size = _swapu32(header.block_size);

    /* Step 2: Extract VHD footer to determine total virtual disk size */
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

    /* Step 3: Load Block Allocation Table (BAT) to map logical to physical blocks */
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

    /* Step 4: Process each logical block in virtual disk (BAT-driven reconstruction)
     * 
     * BAT (Block Allocation Table) Implementation:
     * - BAT is an array of 32-bit big-endian entries, one per logical block
     * - Each BAT entry either points to a physical sector offset OR indicates unallocated
     * - Entry value 0xFFFFFFFF = unallocated block (sparse, contains all zeros)
     * - Other values = sector offset where allocated block begins in VHD file
     * - Our BAT array 'bat[]' loaded from VHD file maps: bat[logical_block] = physical_sector
     * - This allows dynamic VHDs to only store non-zero blocks, achieving space savings
     */
    for (uint32_t i = 0; i < max_table_entries; i++)
    {
        uint32_t block_offset = _swapu32(bat[i]);
        
        if (block_offset == VHD_BAT_ENTRY_UNALLOCATED)
        {
            /* Step 4a: If BAT entry is 0xFFFFFFFF: write zeros (unallocated/sparse block) */
            memset(block_buffer, 0, block_size);
        }
        else
        {
            /* Step 4b: If BAT entry is valid sector offset: convert to file byte offset and read block
             * - BAT entry gives us sector number (multiply by 512 to get byte offset)
             * - At that file location we find: [sector bitmap][actual block data]
             * - We must calculate bitmap size and skip over it to reach the actual data
             */
            uint64_t file_offset = (uint64_t)block_offset * VHD_SECTOR_SIZE;
            
            /* Step 4b.1: Calculate THIS block's sector bitmap size and skip it
             * 
             * VHD Block Structure (each allocated block has this format):
             * [Block's own bitmap][Block's actual data]
             * 
             * Bitmap Purpose & Data Reconstruction:
             * - EVERY allocated block starts with its own sector bitmap
             * - This bitmap tells us which 512-byte sectors within THIS SPECIFIC block contain real data vs zeros
             * - Bit = 1: corresponding sector in this block contains actual data
             * - Bit = 0: corresponding sector in this block should be zeros
             * 
             * Our Implementation Choice (and Safety Analysis):
             * - We COULD parse this block's bitmap and reconstruct sector-by-sector (spec-compliant)
             * - Instead, we simplify: skip this block's bitmap and read the entire block as-is
             * 
             * Safety Analysis:
             * - SAFE: If VHD stores full 2MB of data after bitmap (common case)
             * - UNSAFE: If VHD only stores sectors marked with bit=1 (sparse within block)
             * - RISK: We might read garbage data or get read errors for missing sectors
             * - MITIGATION: Most VHD creators store full blocks to avoid complexity
             * 
             * 
             * TODO: SECURITY/CORRECTNESS - Parse bitmap for full VHD spec compliance
             * - Read bitmap bits to determine which sectors are actually allocated
             * - Only read allocated sectors from VHD, fill unallocated with zeros
             * - Prevents potential read errors and ensures correct reconstruction
             * 
             * Bitmap Size Calculation (for THIS block):
             * - For 2MB block: 2,097,152 ÷ 512 = 4,096 sectors in this block
             * - Need 4,096 bits in this block's bitmap = 512 bytes
             * - VHD requires sector alignment: 512 bytes = 1 sector to skip
             */
            uint32_t sectors_per_block = block_size / VHD_SECTOR_SIZE;
            uint32_t bitmap_size_bits = sectors_per_block;
            uint32_t bitmap_size_bytes = (bitmap_size_bits + 7) / 8;  /* Round up to bytes */
            uint32_t bitmap_size_sectors = (bitmap_size_bytes + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE;  /* Round up to sectors */
            uint64_t data_offset = file_offset + (bitmap_size_sectors * VHD_SECTOR_SIZE);
            
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

        /* Step 4c: Write reconstructed block (zeros or actual data) to output raw image file */
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

/* Extract raw disk image from fixed VHD (internal helper)
 * 
 * Handles the simple fixed VHD extraction process by copying the VHD file
 * and removing the 512-byte footer to produce a raw disk image.
 * 
 * Fixed VHD Extraction Algorithm:
 * 1. Validate input parameters and source VHD file existence
 * 2. Copy entire VHD file to destination using system cp command
 * 3. Remove VHD footer from copy using cvmvhd_remove()
 * 4. Verify final raw image size and report success
 * 
 * Fixed VHD Structure (simple format):
 * [Raw Disk Data - N bytes][VHD Footer - 512 bytes]
 * 
 * Technical Implementation:
 * - Uses system cp command for efficient file copying
 * - Calls cvmvhd_remove() to strip the trailing 512-byte VHD footer
 * - Self-contained operation with proper error handling and cleanup
 * - Output raw image is exactly (VHD_size - 512) bytes
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Hard Disk Footer Format
 * 
 * Additional references:
 * - Fixed VHD Format Structure Documentation
 *   https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd323654(v=vs.85)
 * 
 * @param vhd_file: Path to source fixed VHD file
 * @param raw_file: Path for output raw disk image (will be created/overwritten)
 * @param err: Error context for detailed error reporting  
 * @return: 0 on success, -EINVAL on error
 */
static int _extract_fixed_vhd(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err)
{
    struct stat statbuf;
    buf_t buf = BUF_INITIALIZER;
    
    /* Validate input parameters */
    if (!vhd_file || !raw_file)
    {
        _err(err, "Failed to extract fixed VHD: null parameters");
        return -1;
    }

    /* Verify source VHD exists and get size for logging */
    if (stat(vhd_file, &statbuf) != 0)
    {
        _err(err, "Failed to extract fixed VHD: cannot stat source file %s", vhd_file);
        return -1;
    }

    /* Copy VHD file to destination using system cp command */
    if (execf_return(&buf, "cp %s %s", vhd_file, raw_file) != 0)
    {
        _err(err, "Failed to extract fixed VHD: cannot copy %s to %s", vhd_file, raw_file);
        buf_release(&buf);
        return -1;
    }
    
    buf_release(&buf);

    /* Remove VHD footer from the copy to get raw image */
    if (cvmvhd_remove(raw_file, err) < 0)
    {
        unlink(raw_file); /* Clean up on failure */
        return -1;
    }

    /* Get final size and report success */
    if (stat(raw_file, &statbuf) == 0)
    {
        printf("Extracted %lu bytes from fixed VHD to raw image\n", statbuf.st_size);
    }

    return 0;
}

/*
**==============================================================================
**
** Fixed to Dynamic VHD Conversion Implementation
**
** The following section of function contains helpers and core logic for converting a
** fixed VHD file into a dynamic VHD file.

** References:
** - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
**   https://www.microsoft.com/en-us/download/details.aspx?id=23850
** - VHD Format Specification (Microsoft Docs Legacy)
**   https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd323654(v=vs.85)
** - Virtual Hard Disk Format Specification (GitHub mirror)
**   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
** - Understanding VHD Dynamic Disk Format
**   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
**
**==============================================================================
*/

/* Check if block contains only zero bytes (for VHD sparse optimization) */
static int _is_block_zero(const uint8_t* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        if (data[i] != 0)
            return 0;
    }
    return 1;
}

/* Calculate optimal dynamic VHD parameters based on virtual disk size
 * 
 * Determines the optimal block size, Block Allocation Table (BAT) size, and
 * file layout offsets for creating a dynamic VHD from a fixed VHD. All
 * calculations follow VHD specification requirements for efficiency and
 * compatibility with Microsoft's VHD implementation.
 * 
 * Dynamic VHD File Structure (per VHD Specification v1.0):
 * - Bytes 0-511: Footer copy (VHD_FOOTER_SIZE - identical to footer at end)
 * - Bytes 512-1535: Dynamic header (VHD_DYNAMIC_HEADER_OFFSET to VHD_DYNAMIC_HEADER_OFFSET+1023)
 * - Bytes 1536+: Block Allocation Table (VHD_DYNAMIC_BAT_OFFSET - 4 bytes × max_table_entries)
 * - Bytes BAT_end+: Data blocks (variable size, containing sector bitmaps + actual data)
 * - Bytes file_end-511 to file_end: Footer (VHD_FOOTER_SIZE - standard VHD footer with checksum)
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Block Allocation Table and Data Blocks
 * 
 * Additional references:
 * - Microsoft VHD Developer Blog (technical deep-dive)
 *   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
 * 
 * @param disk_size: Virtual disk size in bytes from source fixed VHD
 * @param block_size: [OUT] Calculated optimal block size (2MB)
 * @param max_table_entries: [OUT] Number of BAT entries needed to cover virtual disk
 * @param table_offset: [OUT] File offset where BAT begins (1536 bytes from start)
 * @return: 0 on success (always succeeds with valid input)
 */
static int _calculate_dynamic_params(
    uint64_t disk_size, 
    uint32_t* block_size, 
    uint32_t* max_table_entries,
    uint64_t* table_offset)
{
    /* Block Size Selection Algorithm:
     * - Uses 2MB as optimal balance of efficiency vs. overhead
     * - Larger blocks = less BAT entries but more wasted space for sparse data
     * - Smaller blocks = more BAT entries but better granularity for sparse data
     * - 2MB is Microsoft's recommended default and widely supported
     */
    *block_size = VHD_DEFAULT_BLOCK_SIZE;
    
    /* BAT Size Calculation:
     * - Each BAT entry is 32-bit (4 bytes) pointing to physical block location
     * - Number of entries = ceil(virtual_disk_size / block_size)
     * - Each BAT entry covers one block, so we need enough entries to cover
     *   the entire logical disk size, rounded up to nearest block boundary
     */
    *max_table_entries = (uint32_t)((disk_size + *block_size - 1) / *block_size);
    
    /* Calculate BAT offset: footer_copy + dynamic_header
     * Per VHD spec, dynamic VHD layout is:
     * [Footer Copy][Dynamic Header][Block Allocation Table][Data Blocks][Footer]
     */
    *table_offset = VHD_DYNAMIC_BAT_OFFSET;
    
    return 0;
}

/* Create properly formatted dynamic VHD header with specification compliance
 * 
 * Constructs a complete 1024-byte dynamic VHD header according to Microsoft's
 * VHD Image Format Specification v1.0. All multi-byte values are converted to
 * big-endian format as required by the specification.
 * 
 * Dynamic VHD Header Structure (1024 bytes, all offsets in big-endian):
 * - Bytes 0-7: Cookie "cxsparse" (identifies dynamic VHD header)
 * - Bytes 8-15: Data offset (0xFFFFFFFF = no additional structures)
 * - Bytes 16-23: Table offset (byte location of Block Allocation Table)
 * - Bytes 24-27: Header version (0x00010000 = VHD specification v1.0)
 * - Bytes 28-31: Max table entries (number of BAT entries for virtual disk)
 * - Bytes 32-35: Block size (data block size, typically 2MB = 0x00200000)
 * - Bytes 36-39: Checksum (one's complement sum of all fields except checksum)
 * - Bytes 40-55: Parent unique ID (all zeros for standalone non-differencing VHDs)
 * - Bytes 56-59: Parent time stamp (zero for standalone VHDs)
 * - Bytes 60-63: Reserved field (must be zero)
 * - Bytes 64-575: Parent Unicode name (zeros for standalone VHDs)
 * - Bytes 576-1023: Parent locator entries (8 entries × 24 bytes, zeros for standalone)
 * 
 * Endianness Handling:
 * - All multi-byte values stored in big-endian (network byte order)
 * - Manual byte-by-byte conversion used for cross-platform compatibility
 * - Critical for interoperability with Microsoft VHD implementations
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Section: Dynamic Disk Header Format
 * 
 * Additional references:
 * - Microsoft Developer Network VHD Format (archived documentation)
 *   https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd323654(v=vs.85)
 * - GitHub libvhdi VHD format documentation and analysis
 *   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
 * 
 * @param header: [OUT] Pointer to header structure to populate
 * @param footer: [IN] VHD footer from source fixed VHD (for UUID preservation)
 * @param max_table_entries: Number of BAT entries needed for virtual disk coverage
 * @param block_size: Size of each data block in bytes (typically 2,097,152)
 * @param table_offset: File byte offset where BAT begins (typically 1536)
 * @return: 0 on success (always succeeds with valid parameters)
 */
static int _create_dynamic_header(
    vhd_dynamic_header_t* header,
    const vhd_footer_t* footer,
    uint32_t max_table_entries,
    uint32_t block_size,
    uint64_t table_offset)
{
    memset(header, 0, sizeof(vhd_dynamic_header_t));
    
    /* Cookie: "cxsparse" - identifies this as a dynamic VHD header */
    memcpy(header->cookie, "cxsparse", 8);
    
    /* Data offset: 0xFFFFFFFFFFFFFFFF (no next structure)
     * This indicates there are no additional dynamic structures after this header
     */
    memset(header->data_offset, 0xFF, 8);
    
    /* Table offset (big-endian) - points to Block Allocation Table
     * VHD spec requires all multi-byte values in big-endian format
     */
    for (int i = 0; i < 8; i++) {
        header->table_offset[i] = (table_offset >> (56 - 8 * i)) & 0xFF;
    }
    
    /* Header version: 0x00010000 (big-endian)
     * Standard version number for dynamic VHD headers
     */
    header->header_version[0] = 0x00;
    header->header_version[1] = 0x01;
    header->header_version[2] = 0x00;
    header->header_version[3] = 0x00;
    
    /* Max table entries and block size (big-endian)
     * Convert from host byte order to big-endian as required by VHD spec
     */
    header->max_table_entries = _swapu32(max_table_entries);
    header->block_size = _swapu32(block_size);
    
    /* Dynamic Header Checksum Calculation (per VHD Specification Section: Dynamic Disk Header Format):
     * NOTE: This checksum covers ONLY the 1024-byte dynamic header
     * 1. Initialize checksum to zero
     * 2. Sum all bytes in this header except the 4-byte checksum field itself
     * 3. Take one's complement of the sum (bitwise NOT operation)
     * 4. Store result in big-endian format in checksum field
     */
    header->checksum = 0; /* Step 1: Initialize checksum to zero */
    uint32_t checksum = 0;
    uint8_t* bytes = (uint8_t*)header;
    /* Step 2: Sum all bytes in header except the checksum field */
    for (size_t i = 0; i < sizeof(vhd_dynamic_header_t); i++) {
        checksum += bytes[i];
    }
    /* Step 3 & 4: Take one's complement and store in big-endian format */
    header->checksum = _swapu32(~checksum);
    
    return 0;
}

/* Convert fixed VHD to dynamic VHD (compaction)
 * 
 * This function implements fixed-to-dynamic VHD conversion according to the
 * Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0.
 * 
 * VHD STRUCTURE CREATED:
 * - Bytes 0-511: Footer copy (VHD_FOOTER_SIZE - identical to footer at end)
 * - Bytes 512-1535: Dynamic header (VHD_DYNAMIC_HEADER_OFFSET to VHD_DYNAMIC_HEADER_OFFSET+1023)
 * - Bytes 1536+: Block Allocation Table (VHD_DYNAMIC_BAT_OFFSET - 4 bytes × max_table_entries)
 * - Bytes BAT_end+: Data blocks (variable size, only allocated blocks)
 *   Each allocated block: [Sector Bitmap][Block Data]
 * - Bytes file_end-511 to file_end: Footer (VHD_FOOTER_SIZE - standard VHD footer with checksum)
 * 
 * ENDIANNESS HANDLING:
 * All multi-byte values in VHD structures must be stored in big-endian format
 * 
 * VHD Specification References:
 * - Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 *   https://www.microsoft.com/en-us/download/details.aspx?id=23850
 *   Sections: Dynamic Disk Header Format, Block Allocation Table and Data Blocks
 * 
 * Additional references:
 * - GitHub libvhdi VHD format documentation and analysis
 *   https://github.com/libyal/libvhdi/blob/main/documentation/Virtual%20Hard%20Disk%20(VHD)%20image%20format.asciidoc
 * - Microsoft VHD Developer Blog (technical deep-dive)
 *   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
 */
int cvmvhd_compact_fixed_to_dynamic(const char* fixed_vhd_file, const char* dynamic_vhd_file, cvmvhd_error_t* err)
{
    int ret = -EINVAL;
    FILE* fixed_stream = NULL;
    FILE* dynamic_stream = NULL;
    vhd_footer_t footer;
    vhd_dynamic_header_t header;
    uint32_t* bat = NULL;
    uint8_t* block_buffer = NULL;
    
    _clear_err(err);
    
    /* Validate input parameters */
    if (!fixed_vhd_file || !dynamic_vhd_file)
    {
        _err(err, "Failed to compact VHD: invalid parameters");
        goto done;
    }
    
    /* Verify input is a fixed VHD */
    cvmvhd_type_t vhd_type = cvmvhd_get_type(fixed_vhd_file, err);
    if (vhd_type != CVMVHD_TYPE_FIXED)
    {
        _err(err, "Failed to compact VHD: input file is not a fixed VHD");
        goto done;
    }
    
    /* Open fixed VHD for reading */
    if (!(fixed_stream = fopen(fixed_vhd_file, "rb")))
    {
        _err(err, "Failed to compact VHD: cannot open fixed VHD file %s", fixed_vhd_file);
        goto done;
    }
    
    /* Step 1: Parse fixed VHD footer to extract disk parameters */
    if (_load_vhd_footer(fixed_stream, &footer) < 0)
    {
        _err(err, "failed to load VHD footer");
        goto done;
    }
    
    /* Extract disk parameters */
    uint64_t disk_size = _swapu64(footer.current_size);
    uint32_t block_size, max_table_entries;
    uint64_t table_offset;
    
    /* Step 2: Calculate optimal dynamic VHD structure (block size, BAT size, offsets)
     * VHD Spec: Dynamic Disk Header Format section - defines BAT structure and block organization
     */
    if (_calculate_dynamic_params(disk_size, &block_size, &max_table_entries, &table_offset) < 0)
    {
        _err(err, "failed to calculate dynamic VHD parameters");
        goto done;
    }
    
    /* Create output dynamic VHD */
    if (!(dynamic_stream = fopen(dynamic_vhd_file, "wb")))
    {
        _err(err, "Failed to compact VHD: cannot create dynamic VHD file %s", dynamic_vhd_file);
        goto done;
    }
    
    /* Step 3: Write footer copy (create dynamic VHD file structure)
     * Dynamic VHDs begin with a copy of the footer at the start of the file.
     * This allows VHD tools to quickly identify the VHD type and parameters.
     * VHD Spec: Hard Disk Footer Format section - footer must appear at start and end
     */
    vhd_footer_t footer_copy = footer;
    /* Update footer for dynamic VHD */
    footer_copy.disk_type = _swapu32(VHD_TYPE_DYNAMIC);
    /* Set data_offset to point to dynamic header (after footer copy)
     * This tells VHD readers where to find the dynamic header structure  
     * [Footer Copy]|---|[Dynamic Header][Block Allocation Table][Data Blocks][Footer]
     */
    uint64_t data_offset = VHD_FOOTER_SIZE;
    for (int i = 0; i < 8; i++) {
        footer_copy.data_offset[i] = (data_offset >> (56 - 8 * i)) & 0xFF;
    }
    
    /* Recalculate checksum after modifying footer fields */
    footer_copy.checksum = _swapu32(_compute_checksum(&footer_copy));
    
    if (fwrite(&footer_copy, sizeof(vhd_footer_t), 1, dynamic_stream) != 1)
    {
        _err(err, "Failed to compact VHD: cannot write footer copy");
        goto done;
    }
    
    /* Step 4: Create and write dynamic header (continued file structure setup) */
    if (_create_dynamic_header(&header, &footer, max_table_entries, block_size, table_offset) < 0)
    {
        _err(err, "Failed to compact VHD: cannot create dynamic header");
        goto done;
    }
    
    if (fwrite(&header, sizeof(vhd_dynamic_header_t), 1, dynamic_stream) != 1)
    {
        _err(err, "Failed to compact VHD: cannot write dynamic header");
        goto done;
    }
    
    /* Step 5: Allocate and initialize BAT
     * The Block Allocation Table (BAT) is an array of 32-bit entries that map
     * logical block numbers to physical sector offsets in the dynamic VHD file.
     * 
     * BAT Entry Values:
     * - 0xFFFFFFFF: Block is unallocated (contains all zeros)
     * - Other values: Sector offset where allocated block begins
     * 
     * VHD Spec: Block Allocation Table and Data Blocks section - defines BAT format and usage
     */
    bat = malloc(max_table_entries * sizeof(uint32_t));
    if (!bat)
    {
        _err(err, "Failed to compact VHD: cannot allocate BAT array");
        goto done;
    }
    
    /* Initialize all BAT entries as unallocated (VHD_BAT_ENTRY_UNALLOCATED) */
    for (uint32_t i = 0; i < max_table_entries; i++)
    {
        bat[i] = _swapu32(VHD_BAT_ENTRY_UNALLOCATED);
    }
    
    printf("Analyzing fixed VHD blocks for compaction...\n");
    
    /* Step 6: Analyze fixed VHD data to identify non-zero blocks
     * 
     * This is the core optimization step where we scan through every logical
     * block in the fixed VHD to determine which blocks contain actual data
     * versus which blocks are entirely zero-filled.
     * 
     * SPACE SAVINGS STRATEGY:
     * - Zero blocks: Not allocated in dynamic VHD, BAT entry remains 0xFFFFFFFF
     * - Non-zero blocks: Allocated sequential space, BAT entry points to location
     * 
     * BLOCK LAYOUT IN DYNAMIC VHD:
     * Each allocated block consists of:
     * 1. Sector bitmap: Shows which 512-byte sectors within block contain data
     * 2. Block data: The actual data sectors (up to block_size bytes)
     * 
     * The bitmap is required by VHD spec even if we mark all sectors as allocated.
     */
    block_buffer = malloc(block_size);
    if (!block_buffer)
    {
        _err(err, "Failed to compact VHD: cannot allocate block buffer");
        goto done;
    }
    
    /* Calculate where first data block will be placed
     * Must account for: footer_copy + dynamic_header + BAT + sector alignment
     */
    uint64_t next_block_offset = table_offset + (max_table_entries * sizeof(uint32_t));
    /* Round up to sector boundary (VHD requirement) */
    next_block_offset = (next_block_offset + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE * VHD_SECTOR_SIZE;
    
    uint32_t allocated_blocks = 0;
    
    /* Scan through all logical blocks in fixed VHD */
    for (uint32_t block_idx = 0; block_idx < max_table_entries; block_idx++)
    {
        uint64_t fixed_offset = (uint64_t)block_idx * block_size;
        size_t read_size = block_size;
        
        /* Handle last block which might be partial - adjust read size and zero-pad remainder */
        if (fixed_offset + block_size > disk_size)
        {
            read_size = disk_size - fixed_offset;
        }
        
        /* Read block from fixed VHD */
        if (fseek(fixed_stream, fixed_offset, SEEK_SET) != 0)
        {
            _err(err, "Failed to compact VHD: cannot seek to block %u in fixed VHD", block_idx);
            goto done;
        }
        
        memset(block_buffer, 0, block_size); 
        if (read_size > 0 && fread(block_buffer, 1, read_size, fixed_stream) != read_size)
        {
            _err(err, "Failed to compact VHD: cannot read block %u from fixed VHD", block_idx);
            goto done;
        }
        
        /* Check if block is all zeros */
        if (!_is_block_zero(block_buffer, block_size))
        {
            /* Block contains data - allocate it in the dynamic VHD
             * 
             * ALLOCATION STRATEGY:
             * 1. Convert byte offset to sector offset (divide by 512)
             * 2. Store in BAT entry in big-endian format
             * 3. Reserve space for bitmap + block data
             * 4. Update next_block_offset for sequential allocation
             */
            bat[block_idx] = _swapu32((uint32_t)(next_block_offset / VHD_SECTOR_SIZE));
            
            /* Calculate space needed: bitmap + block data
             * 
             * BITMAP CALCULATION:
             * - Each bit represents one VHD_SECTOR_SIZE-byte sector
             * - Bitmap size = (sectors_per_block + 7) / 8 bytes (rounded up)
             * - Bitmap must be padded to sector boundary per VHD spec
             */
            uint32_t sectors_per_block = block_size / VHD_SECTOR_SIZE;
            uint32_t bitmap_size_bits = sectors_per_block;
            uint32_t bitmap_size_bytes = (bitmap_size_bits + 7) / 8;
            uint32_t bitmap_size_sectors = (bitmap_size_bytes + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE;
            uint32_t total_block_size = (bitmap_size_sectors + sectors_per_block) * VHD_SECTOR_SIZE;
            
            next_block_offset += total_block_size;
            allocated_blocks++;
        }
        /* else: block is all zeros, leave BAT entry as 0xFFFFFFFF (unallocated) */
    }
    
    printf("Found %u non-zero blocks out of %u total blocks (%.1f%% savings)\n", 
           allocated_blocks, max_table_entries, 
           100.0 * (max_table_entries - allocated_blocks) / max_table_entries);
    
    /* Step 7: Write BAT (Block Allocation Table mapping) */
    if (fseek(dynamic_stream, table_offset, SEEK_SET) != 0)
    {
        _err(err, "Failed to compact VHD: cannot seek to BAT offset");
        goto done;
    }
    
    if (fwrite(bat, sizeof(uint32_t), max_table_entries, dynamic_stream) != max_table_entries)
    {
        _err(err, "Failed to compact VHD: cannot write BAT");
        goto done;
    }
    
    /* Step 8: Write allocated blocks with proper sector bitmaps */
    printf("Writing dynamic VHD blocks...\n");
    
    for (uint32_t block_idx = 0; block_idx < max_table_entries; block_idx++)
    {
        uint32_t bat_entry = _swapu32(bat[block_idx]);
        
        if (bat_entry != VHD_BAT_ENTRY_UNALLOCATED)  /* Block is allocated */
        {
            /* Read block data from fixed VHD */
            uint64_t fixed_offset = (uint64_t)block_idx * block_size;
            size_t read_size = block_size;
            
            if (fixed_offset + block_size > disk_size)
            {
                read_size = disk_size - fixed_offset;
            }
            
            if (fseek(fixed_stream, fixed_offset, SEEK_SET) != 0)
            {
                _err(err, "Failed to compact VHD: cannot seek to block %u in fixed VHD during write", block_idx);
                goto done;
            }
            
            memset(block_buffer, 0, block_size);
            if (read_size > 0 && fread(block_buffer, 1, read_size, fixed_stream) != read_size)
            {
                _err(err, "Failed to compact VHD: cannot read block %u from fixed VHD during write", block_idx);
                goto done;
            }
            
            /* Calculate bitmap parameters */
            uint32_t sectors_per_block = block_size / VHD_SECTOR_SIZE;
            uint32_t bitmap_size_bytes = (sectors_per_block + 7) / 8;
            uint32_t bitmap_size_sectors = (bitmap_size_bytes + VHD_SECTOR_SIZE - 1) / VHD_SECTOR_SIZE;
            
            /* Create bitmap - for simplicity, mark all sectors as allocated
             * 
             * BITMAP FORMAT (per VHD specification):
             * - Each bit represents one 512-byte sector within the block
             * - Bit value 1 = sector is allocated and contains data
             * - Bit value 0 = sector is unallocated (would contain zeros)
             * - Bits are ordered MSB first (bit 7 = sector 0, bit 6 = sector 1, etc.)
             * 
             * OPTIMIZATION NOTE:
             * For maximum compatibility and simplicity, we mark all sectors as
             * allocated rather than implementing per-sector zero detection.
             * This still provides significant space savings at the block level.
             */
            uint8_t* bitmap = calloc(bitmap_size_sectors * VHD_SECTOR_SIZE, 1);
            if (!bitmap)
            {
                _err(err, "Failed to compact VHD: cannot allocate bitmap");
                goto done;
            }
            
            /* Set all bits in bitmap (all sectors allocated)
             * Using MSB-first bit ordering as required by VHD spec
             */
            for (uint32_t bit = 0; bit < sectors_per_block; bit++)
            {
                bitmap[bit / 8] |= (VHD_BITMAP_MSB_MASK >> (bit % 8));  /* MSB first */
            }
            
            /* Seek to block location in dynamic VHD */
            uint64_t block_offset = (uint64_t)bat_entry * VHD_SECTOR_SIZE;
            if (fseek(dynamic_stream, block_offset, SEEK_SET) != 0)
            {
                _err(err, "Failed to compact VHD: cannot seek to block %u offset in dynamic VHD", block_idx);
                free(bitmap);
                goto done;
            }
            
            /* Write bitmap */
            if (fwrite(bitmap, 1, bitmap_size_sectors * VHD_SECTOR_SIZE, dynamic_stream) != bitmap_size_sectors * VHD_SECTOR_SIZE)
            {
                _err(err, "Failed to compact VHD: cannot write bitmap for block %u", block_idx);
                free(bitmap);
                goto done;
            }
            
            /* Write block data */
            if (fwrite(block_buffer, 1, block_size, dynamic_stream) != block_size)
            {
                _err(err, "Failed to compact VHD: cannot write data for block %u", block_idx);
                free(bitmap);
                goto done;
            }
            
            free(bitmap);
        }
    }
    
    /* Step 9: Finalize with VHD footer */
    if (fwrite(&footer_copy, sizeof(vhd_footer_t), 1, dynamic_stream) != 1)
    {
        _err(err, "Failed to compact VHD: cannot write final footer");
        goto done;
    }
    
    printf("Successfully converted fixed VHD to dynamic VHD\n");
    printf("Allocated %u blocks, saved %u blocks\n", allocated_blocks, max_table_entries - allocated_blocks);
    
    ret = 0;

done:
    if (fixed_stream)
        fclose(fixed_stream);
    if (dynamic_stream)
        fclose(dynamic_stream);
    if (bat)
        free(bat);
    if (block_buffer)
        free(block_buffer);

    return ret;
}
