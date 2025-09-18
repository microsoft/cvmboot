// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_CVMVHD_H
#define _CVMBOOT_COMMON_CVMVHD_H

#include <stdint.h>

typedef struct vhd_footer vhd_footer_t;

#define VHD_FOOTER_SIGNATURE "conectix"

/* VHD disk types */
#define VHD_TYPE_NONE       0
#define VHD_TYPE_RESERVED1  1  
#define VHD_TYPE_FIXED      2
#define VHD_TYPE_DYNAMIC    3
#define VHD_TYPE_DIFF       4
#define VHD_TYPE_RESERVED2  5
#define VHD_TYPE_RESERVED3  6

typedef struct disk_geometry
{
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
}
disk_geometry_t;

#define UNIQUE_ID_SIZE 16

/* Dynamic VHD header structure (follows the footer copy at beginning of file) */
typedef struct vhd_dynamic_header
{
    uint8_t cookie[8];           /* "cxsparse" */
    uint8_t data_offset[8];      /* offset to next structure (0xFFFFFFFF if none) */
    uint8_t table_offset[8];     /* offset to Block Allocation Table (BAT) */
    uint8_t header_version[4];   /* header version (0x00010000) */
    uint32_t max_table_entries;  /* maximum entries in BAT */
    uint32_t block_size;         /* size of data block in bytes */
    uint32_t checksum;           /* checksum of dynamic header */
    uint8_t parent_uuid[16];     /* UUID of parent VHD (for differencing disks) */
    uint32_t parent_timestamp;   /* timestamp of parent VHD */
    uint32_t reserved1;          /* reserved */
    uint8_t parent_name[512];    /* Unicode name of parent VHD */
    /* Parent locator entries (8 entries of 24 bytes each) */
    struct {
        uint32_t platform_code;  /* platform code */
        uint32_t platform_data_space; /* space allocated for platform data */
        uint32_t platform_data_length; /* actual length of platform data */
        uint32_t reserved;       /* reserved */
        uint64_t platform_offset; /* offset to platform data */
    } parent_locators[8];
    uint8_t reserved2[256];      /* reserved */
} vhd_dynamic_header_t;

struct vhd_footer
{
    uint8_t cookie[8];
    uint8_t features[4];
    uint8_t format_version[4];
    uint8_t data_offset[8];
    uint32_t timestamp;
    uint8_t creator_application[4];
    uint8_t creator_version[4];
    uint8_t creator_host_os[4];
    uint64_t original_size; /* size of disk in bytes */
    uint64_t current_size; /* size of data in bytes */
    disk_geometry_t disk_geometry;
    uint32_t disk_type;
    uint32_t checksum;
    uint8_t unique_id[UNIQUE_ID_SIZE];
    uint8_t saved_state;
    uint8_t reserved[427];
};

typedef struct cvmvhd_error
{
    char buf[1024];
}
cvmvhd_error_t;

#define CVMVHD_ERROR_INITIALIZER { { '\0' } }

/* VHD type enumeration for detection */
typedef enum {
    CVMVHD_TYPE_UNKNOWN,
    CVMVHD_TYPE_FIXED,
    CVMVHD_TYPE_DYNAMIC
} cvmvhd_type_t;

int cvmvhd_create(const char* vhd_file, size_t size_gb, cvmvhd_error_t* err);

int cvmvhd_resize(const char* vhd_file, size_t size_bytes, cvmvhd_error_t* err);

int cvmvhd_append(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_remove(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_dump(const char* vhd_file, cvmvhd_error_t* err);

/* New functions for dynamic VHD support */
cvmvhd_type_t cvmvhd_get_type(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_read_dynamic_header(const char* vhd_file, vhd_dynamic_header_t* header, cvmvhd_error_t* err);

int cvmvhd_extract_raw_image(const char* vhd_file, const char* raw_file, cvmvhd_error_t* err);

#endif /* _CVMBOOT_COMMON_CVMVHD_H */
