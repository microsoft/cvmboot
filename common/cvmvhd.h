// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_CVMVHD_H
#define _CVMBOOT_COMMON_CVMVHD_H

#include <stdint.h>

typedef struct vhd_footer vhd_footer_t;

#define VHD_FOOTER_SIGNATURE "conectix"

/* VHD disk types as defined in Microsoft VHD specification
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 * Section: Hard Disk Footer Format, Disk Type field
 */
#define VHD_TYPE_NONE       0  /* No disk type specified */
#define VHD_TYPE_RESERVED1  1  /* Reserved (deprecated) */
#define VHD_TYPE_FIXED      2  /* Fixed hard disk - data allocated at creation */
#define VHD_TYPE_DYNAMIC    3  /* Dynamic hard disk - data allocated on demand */
#define VHD_TYPE_DIFF       4  /* Differencing hard disk - stores changes only */
#define VHD_TYPE_RESERVED2  5  /* Reserved for future use */
#define VHD_TYPE_RESERVED3  6  /* Reserved for future use */

/* VHD structure size constants as defined in Microsoft VHD specification
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 */
#define VHD_FOOTER_SIZE         512   /* Section: Hard Disk Footer Format - footer is always 512 bytes */
#define VHD_DYNAMIC_HEADER_SIZE 1024  /* Section: Dynamic Disk Header Format - header is always 1024 bytes */
#define VHD_SECTOR_SIZE         512   /* VHD uses 512-byte sectors throughout (per specification) */

/* Dynamic VHD layout offsets as per VHD specification
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 * 
 * Dynamic VHD structure: [Footer Copy][Dynamic Header][BAT][Data Blocks][Footer]
 */
#define VHD_DYNAMIC_HEADER_OFFSET    VHD_FOOTER_SIZE                                    /* Offset 512: Dynamic header starts after footer copy */
#define VHD_DYNAMIC_BAT_OFFSET      (VHD_FOOTER_SIZE + VHD_DYNAMIC_HEADER_SIZE)        /* Offset 1536: BAT starts after footer copy + dynamic header */

/* Dynamic VHD block size - common implementation choice
 * Reference: While VHD specification allows various block sizes, 2MB is widely used
 * for optimal balance between metadata overhead and compression efficiency
 */
#define VHD_DEFAULT_BLOCK_SIZE      (2 * 1024 * 1024)  /* 2MB = 2097152 bytes */

/* VHD Block Allocation Table (BAT) constants
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 * Section: Block Allocation Table and Data Blocks
 */
#define VHD_BAT_ENTRY_UNALLOCATED   0xFFFFFFFF  /* BAT entry value for unallocated blocks */

/* VHD bitmap manipulation constants
 * Reference: VHD specification sector bitmap format (MSB-first bit ordering)
 */
#define VHD_BITMAP_MSB_MASK         0x80        /* Most significant bit mask for bitmap operations */

/* VHD I/O buffer sizes for optimal performance */
#define VHD_COPY_BUFFER_SIZE        (64 * 1024) /* 64KB buffer for efficient file copying */

/* Implementation-defined security limits (not from VHD specification)
 * These prevent memory exhaustion attacks from malicious VHD files while
 * supporting all legitimate VHD use cases. Values chosen based on practical
 * system resource constraints and common VHD implementation patterns.
 */
#define VHD_MAX_BLOCK_SIZE          (64 * 1024 * 1024)  /* 64MB - security limit for block buffer allocation */
#define VHD_MAX_BAT_ENTRIES         (1024 * 1024)       /* 1M entries - security limit for BAT allocation (supports 2TB with 2MB blocks) */

/* Disk geometry structure as defined in VHD specification
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 * Section: Hard Disk Footer Format, Disk Geometry field
 * 
 * Uses CHS (Cylinder-Head-Sector) addressing scheme for disk geometry.
 * For disks > 127GB, values are calculated using specific algorithm in spec.
 */
typedef struct disk_geometry
{
    uint16_t cylinders;  /* Number of cylinders */
    uint8_t heads;       /* Number of heads per cylinder */
    uint8_t sectors;     /* Number of sectors per track */
}
disk_geometry_t;

#define UNIQUE_ID_SIZE 16

/* Dynamic VHD header structure (follows the footer copy at beginning of file)
 * 
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 * Section: Dynamic Disk Header Format
 *
 * Additional references:
 * - Microsoft Developer Network VHD Format (archived)
 *   https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd323654(v=vs.85)
 * 
 * This header appears immediately after the footer copy at the beginning of dynamic VHDs.
 * Total size: 1024 bytes. All multi-byte values stored in big-endian format.
 */
typedef struct vhd_dynamic_header
{
    uint8_t cookie[8];           /* "cxsparse" - identifies dynamic VHD header */
    uint8_t data_offset[8];      /* offset to next structure (0xFFFFFFFFFFFFFFFF if none) */
    uint8_t table_offset[8];     /* offset to Block Allocation Table (BAT) */
    uint8_t header_version[4];   /* header version (0x00010000 for version 1.0) */
    uint32_t max_table_entries;  /* maximum entries in BAT (big-endian) */
    uint32_t block_size;         /* size of data block in bytes (big-endian, typically 2MB) */
    uint32_t checksum;           /* checksum of dynamic header (big-endian) */
    uint8_t parent_uuid[16];     /* UUID of parent VHD (for differencing disks only) */
    uint32_t parent_timestamp;   /* timestamp of parent VHD modification (big-endian) */
    uint32_t reserved1;          /* reserved field - must be zero */
    uint8_t parent_name[512];    /* Unicode name of parent VHD (UTF-16LE) */
    /* Parent locator entries (8 entries of 24 bytes each = 192 bytes total)
     * Used for differencing disks to locate parent VHD files */
    struct {
        uint32_t platform_code;      /* platform code (big-endian) */
        uint32_t platform_data_space; /* space allocated for platform data (big-endian) */
        uint32_t platform_data_length; /* actual length of platform data (big-endian) */
        uint32_t reserved;           /* reserved field - must be zero */
        uint64_t platform_offset;    /* offset to platform data (big-endian) */
    } parent_locators[8];
    uint8_t reserved2[256];      /* reserved padding - must be zero */
} vhd_dynamic_header_t;

/* VHD Footer structure - appears at end of all VHD files and at beginning of dynamic VHDs
 * 
 * Reference: Microsoft Virtual Hard Disk (VHD) Image Format Specification v1.0
 * https://www.microsoft.com/en-us/download/details.aspx?id=23850
 * Section: Hard Disk Footer Format
 * 
 * Additional references:
 * - Microsoft VHD Blog (developer perspective)
 *   https://blogs.msdn.microsoft.com/virtual_pc_guy/2007/10/11/understanding-dynamic-vhd/
 * 
 * Total size: 512 bytes. All multi-byte values stored in big-endian format.
 * For dynamic VHDs, this footer is duplicated at both start and end of file.
 */
struct vhd_footer
{
    uint8_t cookie[8];           /* "conectix" - VHD file signature */
    uint8_t features[4];         /* feature flags (big-endian) */
    uint8_t format_version[4];   /* file format version (big-endian, 0x00010000) */
    uint8_t data_offset[8];      /* offset to next structure (0xFFFFFFFFFFFFFFFF for fixed) */
    uint32_t timestamp;          /* creation timestamp (big-endian, seconds since 2000-01-01) */
    uint8_t creator_application[4]; /* creating application ("vpc " for Virtual PC) */
    uint8_t creator_version[4];  /* creator version (big-endian) */
    uint8_t creator_host_os[4];  /* creator host OS ("Wi2k", "Mac ", etc.) */
    uint64_t original_size;      /* original size of disk in bytes (big-endian) */
    uint64_t current_size;       /* current size of data in bytes (big-endian) */
    disk_geometry_t disk_geometry; /* disk geometry (CHS format) */
    uint32_t disk_type;          /* disk type (fixed=2, dynamic=3, etc.) (big-endian) */
    uint32_t checksum;           /* checksum of footer (big-endian) */
    uint8_t unique_id[UNIQUE_ID_SIZE]; /* unique VHD identifier (UUID/GUID) */
    uint8_t saved_state;         /* saved state flag */
    uint8_t reserved[427];       /* reserved padding - must be zero */
};

typedef struct cvmvhd_error
{
    char buf[1024];
}
cvmvhd_error_t;

#define CVMVHD_ERROR_INITIALIZER { { '\0' } }

/* VHD type enumeration for VHD format detection and classification
 * 
 * Used by cvmvhd_get_type() to identify VHD format variants based on header analysis.
 * Supports the two primary VHD formats defined in the Microsoft specification.
 */
typedef enum {
    CVMVHD_TYPE_UNKNOWN = VHD_TYPE_NONE,  /* Unable to determine VHD type or invalid VHD */
    CVMVHD_TYPE_FIXED = VHD_TYPE_FIXED,    /* Fixed VHD - all space pre-allocated */
    CVMVHD_TYPE_DYNAMIC = VHD_TYPE_DYNAMIC   /* Dynamic VHD - space allocated on demand */
} cvmvhd_type_t;

int cvmvhd_create(const char* vhd_file, size_t size_gb, cvmvhd_error_t* err);

int cvmvhd_resize(const char* vhd_file, size_t size_bytes, cvmvhd_error_t* err);

int cvmvhd_append(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_remove(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_dump(const char* vhd_file, cvmvhd_error_t* err);

cvmvhd_type_t cvmvhd_get_type(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_read_dynamic_header(const char* vhd_file, vhd_dynamic_header_t* header, cvmvhd_error_t* err);

int cvmvhd_extract_raw_image(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err);

int cvmvhd_compact_fixed_to_dynamic(const char* fixed_vhd_file, const char* dynamic_vhd_file, cvmvhd_error_t* err);

#endif /* _CVMBOOT_COMMON_CVMVHD_H */
