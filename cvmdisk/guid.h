// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_GUID_H
#define _CVMBOOT_CVMDISK_GUID_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define GUID_STRING_LENGTH 36
#define GUID_STRING_SIZE (GUID_STRING_LENGTH + 1)
#define GUID_BYTES 16
#define GUID_INITIALIZER { 0 }

typedef struct
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
}
guid_t;

typedef struct guid_string
{
    char buf[GUID_STRING_SIZE];
}
guid_string_t;

int guid_generate(guid_t* guid);

int guid_init_xy(guid_t* guid, uint64_t x, uint64_t y);

int guid_init_bytes(guid_t* guid, const uint8_t bytes[GUID_BYTES]);

int guid_init_str(guid_t* guid, const char* str);

int guid_get_xy(const guid_t* guid, uint64_t* x, uint64_t* y);

int guid_get_bytes(const guid_t* guid, uint8_t bytes[GUID_BYTES]);

int guid_format(guid_string_t* str, const guid_t* guid);

int guid_valid_str(const char* str);

bool guid_null(const guid_t* guid);

void guid_clear(guid_t* guid);

bool guid_equal(const guid_t* x, const guid_t* y);

void guid_dump(const guid_t* guid);

extern const guid_t mbr_type_guid;
extern const guid_t efi_type_guid;
extern const guid_t linux_type_guid;
extern const guid_t rootfs_upper_type_guid;
extern const guid_t efi_upper_type_guid;
extern const guid_t thin_data_type_guid;
extern const guid_t thin_meta_type_guid;
extern const guid_t verity_type_guid;

#endif /* _CVMBOOT_CVMDISK_GUID_H */
