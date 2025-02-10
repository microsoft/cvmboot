// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_CVMVHD_H
#define _CVMBOOT_COMMON_CVMVHD_H

#include <stdint.h>

typedef struct vhd_footer vhd_footer_t;

#define VHD_FOOTER_SIGNATURE "conectix"

typedef struct disk_geometry
{
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
}
disk_geometry_t;

#define UNIQUE_ID_SIZE 16

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

int cvmvhd_create(const char* vhd_file, size_t size_gb, cvmvhd_error_t* err);

int cvmvhd_resize(const char* vhd_file, size_t size_bytes, cvmvhd_error_t* err);

int cvmvhd_append(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_remove(const char* vhd_file, cvmvhd_error_t* err);

int cvmvhd_dump(const char* vhd_file, cvmvhd_error_t* err);

#endif /* _CVMBOOT_COMMON_CVMVHD_H */
