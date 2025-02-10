// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_GPT_H
#define _CVMBOOT_CVMDISK_GPT_H

#include <stddef.h>
#include <stdint.h>
#include "guid.h"
#include "blockdev.h"
#include "defs.h"

//
// This class encapsulates a GUID Partition Table (GPT). Raw disks have
// two copies of the GPT: one at the start and another at the end.
//
//     +--------------+
//     | MBR          |
//     +--------------+
//     | GPT-header   |
//     +--------------+
//     | GPT-entries  |
//     +--------------+
//     |              | <- first_usable_lba (34th 512-byte block)
//     +--------------+
//     |              |
//     +--------------+
//     |              |
//     +--------------+
//     |              |
//     +--------------+
//     |              | <- last_usable_lba (totals blocks minus 34)
//     +--------------+
//     | GPT-entries  |
//     +--------------+
//     | GPT-header   |
//     +--------------+
//
// After the disk size is extended or shrunk, the trailing GPT will be in the
// wrong position. To compensate, the last_usable_lba is recalculated by taking
// the total size the device (in 512-byte blocks) and subtracting 34 blocks.
// When the GPTs are rewritten, the situation is corrected.
//

#define GPT_MAX_ENTRIES_SIZE (GPT_MAX_ENTRIES * sizeof(gpt_entry_t))
#define GPT_MAX_ENTRIES_BLOCKS (GPT_MAX_ENTRIES_SIZE / GPT_BLOCK_SIZE)
#define GPT_SECTOR_SIZE 512
#define GPT_MBR_SIZE 512
#define GPT_SIGNATURE_SIZE 8
#define GPT_MAX_ENTRIES 128
#define GPT_ENTRY_TYPENAME_SIZE 36
#define GPT_BLOCK_SIZE 512

typedef struct
{
    uint8_t _mbr[GPT_MBR_SIZE];
}
gpt_mbr_t;

typedef struct gpt_header
{
    char signature[GPT_SIGNATURE_SIZE];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t primary_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint64_t unique_guid1;
    uint64_t unique_guid2;
    uint64_t first_entry_lba;
    uint32_t number_of_entries;
    uint32_t size_of_entry;
    uint32_t entries_crc32;
    uint8_t padding[420];
}
gpt_header_t;

typedef struct gpt_entry
{
    uint64_t type_guid1;
    uint64_t type_guid2;
    uint64_t unique_guid1;
    uint64_t unique_guid2;
    uint64_t starting_lba;
    uint64_t ending_lba;
    uint64_t attributes;
    uint16_t type_name[GPT_ENTRY_TYPENAME_SIZE];
}
gpt_entry_t;

void gpt_header_dump(
    const gpt_header_t* h,
    bool concise,
    void (*colorize)(int));

void gpt_entry_dump(
    const gpt_entry_t* e,
    bool concise,
    void (*colorize)(int));

/* get entry offset in bytes */
inline size_t gpt_entry_offset(const gpt_entry_t* e)
{
    return e->starting_lba * GPT_BLOCK_SIZE;
}

/* get entry size in bytes */
inline size_t gpt_entry_size(const gpt_entry_t* e)
{
    return (e->ending_lba - e->starting_lba + 1) * GPT_BLOCK_SIZE;
}

typedef struct
{
    size_t _num_entries;
    struct primary
    {
        gpt_mbr_t mbr;
        gpt_header_t header;
        gpt_entry_t entries[GPT_MAX_ENTRIES];
    }
    _primary;
    struct backup
    {
        gpt_entry_t entries[GPT_MAX_ENTRIES];
        gpt_header_t header;
    }
    _backup;
    blockdev_t* _blockdev;
    int _openflags;
}
gpt_t;

void gpt_create(gpt_t* gpt, blockdev_t* blockdev);

void gpt_release(gpt_t* gpt);

int gpt_sync(gpt_t* gpt);

size_t gpt_find_partition(gpt_t* gpt, const guid_t* unique_guid);

ssize_t gpt_find_type_partition(const gpt_t* gpt, const guid_t* type_guid);

int gpt_get_entry(const gpt_t* gpt, size_t index, gpt_entry_t* entry);

INLINE void gpt_get_entries(
    const gpt_t* gpt,
    gpt_entry_t entries[GPT_MAX_ENTRIES],
    size_t* num_entries)
{
    size_t n = 0;

    for (size_t i = 0; i < gpt->_num_entries; i++)
    {
        entries[i] = gpt->_primary.entries[i];
        n++;
    }

    *num_entries = n;
}

INLINE size_t gpt_get_num_entries(const gpt_t* gpt)
{
    return gpt->_num_entries;
}

ssize_t gpt_remove_partition(gpt_t* gpt, size_t index);

ssize_t gpt_shrink_partition(gpt_t* gpt, size_t index, size_t num_sectors);

// if num_sectors is 0, then use maximum space available:
ssize_t gpt_resize_partition(gpt_t* gpt, size_t index, size_t num_sectors);

int gpt_add_partition(
    gpt_t* gpt,
    const guid_t* type_guid,
    const guid_t* unique_guid,
    uint64_t num_blocks,
    uint64_t attributes,
    const uint16_t type_name[GPT_ENTRY_TYPENAME_SIZE]);

int gpt_remove_partitions(gpt_t* gpt, const guid_t* type_guid, bool trace);

void gpt_dump(const gpt_t* gpt);

void gpt_dump_concise(const gpt_t* gpt);

int gpt_open(const char* pathname, int flags, gpt_t** gpt);

void gpt_close(gpt_t* gpt);

/* ask kernel to reload this GUID partition table */
int gpt_reload(gpt_t* gpt);

ssize_t gpt_trailing_free_space(const gpt_t* gpt);

int find_gpt_entry_by_type(
    const char* disk,
    const guid_t* type,
    char part[PATH_MAX],
    gpt_entry_t* entry);

int gpt_add_entry(gpt_t* gpt, const gpt_entry_t* entry);

int gpt_is_sorted(const gpt_t* gpt);

#endif /* _CVMBOOT_CVMDISK_GPT_H */
