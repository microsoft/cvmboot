// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <linux/fs.h>
#include <checksum.h>
#include <sys/ioctl.h>
#include <time.h>
#include "guid.h"
#include "blockdev.h"
#include "eraise.h"
#include "colors.h"
#include "gpt.h"
#include "loop.h"

/*
**==============================================================================
**
** local definitions:
**
**==============================================================================
*/

_Static_assert(sizeof(gpt_mbr_t) == 512, __FILE__);
_Static_assert(sizeof(gpt_header_t) == 512, __FILE__);
_Static_assert(sizeof(gpt_entry_t) == 128, __FILE__);

static const char _signature[8] = { 'E', 'F', 'I', ' ' , 'P', 'A', 'R', 'T' };

static uint32_t _crc32(const void* p, size_t n)
{
    return crc_32(p, n);
}

static void _ind(size_t n)
{
    for (size_t i = 0; i < n; i++)
        printf("    ");
}

__attribute__((format(printf, 2, 3)))
static int _iprintf(size_t depth, const char* fmt, ...)
{
    _ind(depth);
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

static void _swap_u64(uint64_t* x, uint64_t* y)
{
    uint64_t t = *x;
    *x = *y;
    *y = t;
}

static bool _check_signature(const gpt_header_t* header)
{
    return memcmp(header->signature, _signature, GPT_SIGNATURE_SIZE) == 0;
}

static bool _entry_is_null(const gpt_entry_t* e)
{
    return e->type_guid1 == 0 && e->type_guid2 == 0;
}

static size_t _count_entries(const gpt_entry_t* entries)
{
    size_t count = 0;

    for (size_t i = 0; i < GPT_MAX_ENTRIES; i++)
    {
        const gpt_entry_t* e = &entries[i];

        if (e->type_guid1 == 0 && e->type_guid2 == 0)
            break;

        count++;
    }

    return count;
}

static void _update_header_crc32(gpt_header_t* header)
{
    gpt_header_t* h = header;
    h->header_crc32 = 0;
    header->header_crc32 = _crc32((uint8_t*)h, h->header_size);
}

static uint32_t _entries_crc(const gpt_entry_t entries[GPT_MAX_ENTRIES])
{
    return _crc32((uint8_t*)entries, GPT_MAX_ENTRIES_SIZE);
}

static void _generate_backup_header(
    const gpt_header_t* primary_header,
    gpt_header_t* backup_header)
{
    *backup_header = *primary_header;

    // Swap the primary_lba and backup_lba.
    _swap_u64(&backup_header->primary_lba, &backup_header->backup_lba);

    backup_header->first_entry_lba = backup_header->last_usable_lba + 1;

    /* update the header->header_crc32 field */
    _update_header_crc32(backup_header);
}

static void _update_primary_header(
    gpt_header_t* header,
    uint64_t new_last_usable_lba)
{
    size_t backup_size = GPT_MAX_ENTRIES_SIZE + sizeof(gpt_header_t);
    size_t backup_blocks = backup_size / GPT_BLOCK_SIZE;

    header->last_usable_lba = new_last_usable_lba;
    header->backup_lba = new_last_usable_lba + backup_blocks;
    _update_header_crc32(header);
}

static int _gpt_update_crcs(gpt_t* gpt)
{
    int ret = 0;
    const uint32_t primary_entries_crc32 = _entries_crc(gpt->_primary.entries);
    const uint32_t backup_entries_crc32 = _entries_crc(gpt->_backup.entries);

    if (primary_entries_crc32 != backup_entries_crc32)
        ERAISE(-EINVAL);

    gpt->_primary.header.entries_crc32 = primary_entries_crc32;
    gpt->_backup.header.entries_crc32 = backup_entries_crc32;

    _update_header_crc32(&gpt->_primary.header);
    _update_header_crc32(&gpt->_backup.header);

done:
     return ret;
}

/* passed to qsort() below */
static int _compare_entries(const void* lhs, const void* rhs)
{
    const gpt_entry_t* entry1 = ((gpt_entry_t*)lhs);
    const gpt_entry_t* entry2 = ((gpt_entry_t*)rhs);
    uint64_t x = entry1->starting_lba;
    uint64_t y = entry2->starting_lba;

    /* treat zero starting-lba as maximum value */
    if (_entry_is_null(entry1))
        x = UINT64_MAX;

    /* treat zero starting-lba as maximum value */
    if (_entry_is_null(entry2))
        y = UINT64_MAX;

    if (x < y)
        return -1;

    if (x > y)
        return 1;

    return 0;
}

/*
**==============================================================================
**
** public interface:
**
**==============================================================================
*/

void gpt_create(gpt_t* gpt, blockdev_t* blockdev)
{
    gpt->_num_entries = 0;
    gpt->_blockdev = blockdev;
    memset(&gpt->_primary, 0, sizeof(gpt->_primary));
    memset(&gpt->_backup, 0, sizeof(gpt->_backup));
}

void gpt_release(gpt_t* gpt)
{
    blockdev_close(gpt->_blockdev);
}

static int _gpt_load(gpt_t* gpt)
{
    int ret = 0;

    gpt->_num_entries = 0;

    // Read the MBR and primary GPT blocks.
    {
        const size_t lba = 0;
        const size_t nblocks = sizeof(gpt->_primary) / GPT_BLOCK_SIZE;
        ECHECK(blockdev_get(gpt->_blockdev, lba, &gpt->_primary, nblocks));
    }

    // Check the signature.
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    if (gpt->_openflags != O_RDONLY)
    {
        // Sort the entries in the primary header by starting lba
        qsort(gpt->_primary.entries, GPT_MAX_ENTRIES, sizeof(gpt_entry_t),
            _compare_entries);
    }

    // Count the entires.
    gpt->_num_entries = _count_entries(gpt->_primary.entries);

    // Recalculate last_usable_lba (if the disk has been extended, this
    // value will not match the current value of last_usable_lba).
    uint64_t new_last_usable_lba;
    {
        uint64_t total_size;

        ECHECK((total_size = blockdev_get_size(gpt->_blockdev)));
        uint64_t total_blocks = total_size / GPT_BLOCK_SIZE;
        uint64_t backup_blocks = sizeof(gpt->_backup) / GPT_BLOCK_SIZE;
        new_last_usable_lba = total_blocks - backup_blocks - 1;
    }

    // if the disk has not been resized then these will match.
    if (new_last_usable_lba == gpt->_primary.header.last_usable_lba)
    {
        const size_t lba = new_last_usable_lba + 1;
        const size_t nblocks = sizeof(gpt->_backup) / GPT_BLOCK_SIZE;
        ECHECK(blockdev_get(gpt->_blockdev, lba, &gpt->_backup, nblocks));

        // Check the signature.
        if (!_check_signature(&gpt->_backup.header))
            ERAISE(-EINVAL);

        if (gpt->_openflags != O_RDONLY)
        {
            // Sort the entries in the backup header by starting lba
            qsort(gpt->_backup.entries, GPT_MAX_ENTRIES, sizeof(gpt_entry_t),
                _compare_entries);
        }

        // Perform sanity check by regenerating the primary header.
        {
            gpt_header_t h = gpt->_primary.header;
            _update_primary_header(&h, new_last_usable_lba);

            if (memcmp(&h, &gpt->_primary.header, sizeof(h)) != 0)
                ERAISE(-EINVAL);
        }

        // Perform sanity check by regenerating the backup header.
        {
            gpt_header_t h;
            _generate_backup_header(&gpt->_primary.header, &h);

            if (memcmp(&h, &gpt->_backup.header, sizeof(h)) != 0)
                ERAISE(-EINVAL);
        }

        // Perform sanity check by comparing the two entries[] arrays.
        if (memcmp(
            gpt->_primary.entries,
            gpt->_backup.entries,
            GPT_MAX_ENTRIES_SIZE) != 0)
        {
            ERAISE(-EINVAL);
        }
    }
    else
    {
        // Update the primary header.
        gpt_header_t h1 = gpt->_primary.header;
        _update_primary_header(&h1, new_last_usable_lba);
        gpt->_primary.header = h1;

        // Generate the backup header.
        gpt_header_t h2 = gpt->_primary.header;
        _generate_backup_header(&h1, &h2);
        gpt->_backup.header = h2;

        // Copy the primary entries to the backup entries.
        memcpy(gpt->_backup.entries, gpt->_primary.entries,
            GPT_MAX_ENTRIES_SIZE);
    }

    if (gpt->_openflags != O_RDONLY)
        ECHECK(_gpt_update_crcs(gpt));

done:

    return ret;
}

int gpt_reload(gpt_t* gpt)
{
    int ret = 0;

    if (!gpt)
        ERAISE(-EINVAL);

    // Force the kernel re-read the partition table. This often fails
    // on the first couple attempts, so retry several times.
    {
        const size_t max_retries = 128;
        long msec = 2000000; /* 2 msec expressed in nsec */
        bool okay = false;

        for (size_t i = 0; i < max_retries; i++)
        {
            if (ioctl(gpt->_blockdev->fd, BLKRRPART, NULL) == 0)
            {
                okay = true;
                break;
            }
        }

        if (okay)
            goto done;

        /* This will run for a maxiumum of about 1 second (1024 msec) */
        for (size_t i = 0; i < 10; i++)
        {
            struct timespec req = { 0, msec };

            if (ioctl(gpt->_blockdev->fd, BLKRRPART, NULL) == 0)
            {
                okay = true;
                break;
            }

            nanosleep(&req, NULL);
            msec *= 2;
        }

        if (!okay)
            ERAISE(-errno);
    }

done:
    return ret;
}

int gpt_sync(gpt_t* gpt)
{
    int ret = 0;

    // Fail if the primary GPT has not been loaded.
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    // Fail if the backup GPT has not been loaded.
    if (!_check_signature(&gpt->_backup.header))
        ERAISE(-EINVAL);

    // Fail if offset of backup lba looks wrong.
    {
        const uint64_t last_usable_lba = gpt->_primary.header.last_usable_lba;
        const uint64_t backup_lba = gpt->_primary.header.backup_lba;
        size_t offset1 = (last_usable_lba + 1) * GPT_BLOCK_SIZE;
        size_t offset2 = (backup_lba * GPT_BLOCK_SIZE) - GPT_MAX_ENTRIES_SIZE;

        if (offset1 != offset2)
            ERAISE(-EINVAL);
    }

    // Sync the primary GPT:
    {
        const size_t lba = 0;
        const size_t nblocks = sizeof(gpt->_primary) / GPT_BLOCK_SIZE;
        ECHECK(blockdev_put(gpt->_blockdev, lba, &gpt->_primary, nblocks));
    }

    // Sync the backup GPT:
    {
        size_t lba = gpt->_primary.header.backup_lba - GPT_MAX_ENTRIES_BLOCKS;
        const size_t nblocks = sizeof(gpt->_backup) / GPT_BLOCK_SIZE;
        ECHECK(blockdev_put(gpt->_blockdev, lba, &gpt->_backup, nblocks));
    }

    ECHECK(gpt_reload(gpt));
done:

    return ret;
}

static void _gpt_dump_header(const gpt_header_t* h, size_t n)
{
    _iprintf(n, "header\n");
    _iprintf(n, "{\n");
    n++;

    _iprintf(n, "signature: \"");
    for (size_t i = 0; i < GPT_SIGNATURE_SIZE; i++)
        printf("%c", h->signature[i]);
    printf("\"\n");

    _iprintf(n, "revision: %u\n", h->revision);
    _iprintf(n, "header_size: %u\n", h->header_size);
    _iprintf(n, "header_crc32: %u\n", h->header_crc32);
    _iprintf(n, "primary_lba: %lu\n", h->primary_lba);
    _iprintf(n, "backup_lba: %lu\n", h->backup_lba);
    _iprintf(n, "first_usable_lba: %lu\n", h->first_usable_lba);
    _iprintf(n, "last_usable_lba: %lu\n", h->last_usable_lba);

    guid_t guid;
    guid_init_xy(&guid, h->unique_guid1, h->unique_guid2);

    {
        guid_string_t str;
        guid_format(&str, &guid);
        _iprintf(n, "unique_guid: %s\n", str.buf);
    }

    _iprintf(n, "first_entry_lba: %lu\n", h->first_entry_lba);
    _iprintf(n, "number_of_entries: %u\n", h->number_of_entries);
    _iprintf(n, "size_of_entry: %u\n", h->size_of_entry);
    _iprintf(n, "entries_crc32: %u\n", h->entries_crc32);

    n--;
    _iprintf(n, "}\n");
}

static void _gpt_dump_entry(const gpt_entry_t* e, size_t n)
{
    guid_t guid;
    guid_string_t str;

    _iprintf(n, "entry\n");
    _iprintf(n, "{\n");
    n++;

    guid_init_xy(&guid, e->type_guid1, e->type_guid2);
    guid_format(&str, &guid);
    _iprintf(n, "type_guid: %s\n", str.buf);
    _iprintf(n, "type_guid1: %lx\n", e->type_guid1);
    _iprintf(n, "type_guid2: %lx\n", e->type_guid2);

    guid_init_xy(&guid, e->unique_guid1, e->unique_guid2);
    guid_format(&str, &guid);
    _iprintf(n, "unique_guid: %s\n", str.buf);
    _iprintf(n, "unique_guid1: %lx\n", e->unique_guid1);
    _iprintf(n, "unique_guid2: %lx\n", e->unique_guid2);

    _iprintf(n, "starting_lba: %ld\n", e->starting_lba);
    _iprintf(n, "ending_lba: %ld\n", e->ending_lba);
    _iprintf(n, "attributes: %ld\n", e->attributes);

    /* typename */
    {
        _iprintf(n, "typename: \"");

        for (size_t i = 0; i < GPT_ENTRY_TYPENAME_SIZE; i++)
        {
            char c = (char)e->type_name[i];

            if (!c)
                break;

            printf("%c", c);
        }

        printf("\"\n");
    }

    n--;
    _iprintf(n, "}\n");
}

static void _gpt_dump_entries(
    const gpt_entry_t* entries,
    size_t num_entries,
    size_t n)
{
    _iprintf(n, "entries\n");
    _iprintf(n, "{\n");
    n++;

    _iprintf(n, "num_entries: %ld\n", num_entries);

    for (size_t i = 0; i < num_entries; i++)
        _gpt_dump_entry(&entries[i], n);

    n--;
    _iprintf(n, "}\n");
}

static void _gpt_dump(
    const gpt_t* gpt,
    const char* type,
    const gpt_header_t* header,
    const gpt_entry_t entries[GPT_MAX_ENTRIES],
    size_t n)
{
    _iprintf(n, "%s gpt\n", type);
    _iprintf(n, "{\n");
    n++;

    _gpt_dump_header(header, n);
    _gpt_dump_entries(entries, gpt->_num_entries, n);

    n--;
    _iprintf(n, "}\n");
}

void gpt_dump(const gpt_t* gpt)
{
    _gpt_dump(gpt, "primary", &gpt->_primary.header, gpt->_primary.entries, 0);
    _gpt_dump(gpt, "backup", &gpt->_backup.header, gpt->_backup.entries, 0);
}

static void _colorize(int color)
{
    switch (color)
    {
        case 0:
            printf("%s", colors_reset);
            break;
        case 1:
            printf("%s", colors_green);
            break;
        case 2:
            printf("%s", colors_yellow);
            break;
    }
}

void gpt_dump_concise(const gpt_t* gpt)
{
    printf("%sHEADER%s", colors_cyan, colors_reset);
    gpt_header_dump(&gpt->_primary.header, true, _colorize);

    for (size_t i = 0; i < gpt->_num_entries; i++)
    {
        printf("%sENTRY%s", colors_cyan, colors_reset);
        gpt_entry_dump(&gpt->_primary.entries[i], true, _colorize);
    }
}

int gpt_add_partition(
    gpt_t* gpt,
    const guid_t* type_guid,
    const guid_t* unique_guid,
    uint64_t num_blocks,
    uint64_t attributes,
    const uint16_t type_name[GPT_ENTRY_TYPENAME_SIZE])
{
    int ret = 0;
    gpt_entry_t entry;

    // Fail if the primary GPT has not been loaded.
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    if (guid_null(type_guid) || guid_null(unique_guid))
        ERAISE(-EINVAL);

    if (gpt->_num_entries == GPT_MAX_ENTRIES)
        ERAISE(-ERANGE);

    memset(&entry, 0, sizeof(entry));
    guid_get_xy(type_guid, &entry.type_guid1, &entry.type_guid2);
    guid_get_xy(unique_guid, &entry.unique_guid1, &entry.unique_guid2);
    entry.attributes = attributes;

    if (type_name)
        memcpy(entry.type_name, type_name, sizeof(entry.type_name));

    // ATTN: consider optimizing to search for gaps either before the
    // first partition or between partitions. For now consider only the
    // gap after the last partition and the end of the device. This will
    // suffice when all trailing partitions are added and removed as a whole.

    // Use the gap between the last partition and the backup GPT.
    {
        size_t max_ending_lba = gpt->_primary.header.first_usable_lba;

        // Find the maximum ending_lba of any partition.
        for (size_t i = 0; i <  gpt->_num_entries; i++)
        {
            const gpt_entry_t* e =  &gpt->_primary.entries[i];

            if (e->ending_lba > max_ending_lba)
                max_ending_lba = e->ending_lba;
        }

        const size_t starting_lba = max_ending_lba + 1;
        const size_t ending_lba = gpt->_primary.header.last_usable_lba;
        const size_t gap = ending_lba - starting_lba;

        if (ending_lba <= starting_lba)
            ERAISE(-EINVAL);

        if (gap < num_blocks)
            ERAISE(-ENOSPC);

        entry.starting_lba = starting_lba;

        if (num_blocks == 0)
        {
            // Partitions must be aligned on 2048 512-byte sectors (1 MB)
            entry.ending_lba = gpt->_primary.header.last_usable_lba;
            entry.ending_lba &= ~0x7ff; /* 0x7ff == 2048 - 1 */
            entry.ending_lba--;
        }
        else
        {
            /* sector must end on a multiple of 2048 blocks */
#if 1
            num_blocks = (num_blocks + 2047) / 2048 * 2048;
#endif
            entry.ending_lba = starting_lba + num_blocks - 1;

            if (entry.ending_lba > gpt->_primary.header.last_usable_lba)
            {
                ERAISE(-ERANGE);
            }
        }

        gpt->_primary.entries[gpt->_num_entries] = entry;
        gpt->_backup.entries[gpt->_num_entries] = entry;
        gpt->_num_entries++;

        ECHECK(_gpt_update_crcs(gpt));
    }

    /* sync changes to device */
    ECHECK(gpt_sync(gpt));

done:

    return ret;
}

int gpt_add_entry(gpt_t* gpt, const gpt_entry_t* entry)
{
    int ret = 0;
    guid_t unique_guid;
    gpt_entry_t new_entry;

    if (!entry)
        ERAISE(-EINVAL);

    // If no room for this entry
    if (entry->ending_lba >= gpt->_primary.header.last_usable_lba)
        ERAISE(-EINVAL);

    // Fail if the primary GPT has not been loaded
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    // Fail if no more entries slots
    if (gpt->_num_entries == GPT_MAX_ENTRIES)
        ERAISE(-ERANGE);

    new_entry = *entry;
    guid_generate(&unique_guid);
    guid_get_xy(&unique_guid, &new_entry.unique_guid1, &new_entry.unique_guid2);
    gpt->_primary.entries[gpt->_num_entries] = new_entry;
    gpt->_backup.entries[gpt->_num_entries] = new_entry;
    gpt->_num_entries++;

    ECHECK(_gpt_update_crcs(gpt));
    ECHECK(gpt_sync(gpt));

done:
    return ret;
}

size_t gpt_find_partition(gpt_t* gpt, const guid_t* unique_guid)
{
    for (size_t i = 0; i < gpt->_num_entries; i++)
    {
        const gpt_entry_t* e = &gpt->_primary.entries[i];
        guid_t tmp_guid;

        guid_init_xy(&tmp_guid, e->unique_guid1, e->unique_guid2);

        if (guid_equal(&tmp_guid, unique_guid))
            return i;
    }

    // Not found!
    return (size_t)-1;
}

ssize_t gpt_remove_partition(gpt_t* gpt, size_t index)
{
    ssize_t ret = 0;

    if (index >= gpt->_num_entries)
        ERAISE(-ERANGE);

    {
        const size_t remaining = gpt->_num_entries - index - 1;

        if (remaining)
        {
            memmove(
                &gpt->_primary.entries[index],
                &gpt->_primary.entries[index+1],
                remaining * sizeof(gpt_entry_t));
            memmove(
                &gpt->_backup.entries[index],
                &gpt->_backup.entries[index+1],
                remaining * sizeof(gpt_entry_t));
        }
    }

    gpt->_num_entries--;
    memset(&gpt->_primary.entries[gpt->_num_entries], 0, sizeof(gpt_entry_t));
    memset(&gpt->_backup.entries[gpt->_num_entries], 0, sizeof(gpt_entry_t));

    // Update the header CRCs.
    ECHECK(_gpt_update_crcs(gpt));

    ret = index;

done:
    return ret;
}

ssize_t gpt_find_type_partition(const gpt_t* gpt, const guid_t* type_guid)
{
    for (size_t i = 0; i < gpt->_num_entries; i++)
    {
        const gpt_entry_t* e = &gpt->_primary.entries[i];
        guid_t tmp_guid;
        guid_init_xy(&tmp_guid, e->type_guid1, e->type_guid2);

        if (guid_equal(&tmp_guid, type_guid))
            return i;
    }

    // Not found!
    return -ENOENT;
}

int gpt_remove_partitions(gpt_t* gpt, const guid_t* type_guid, bool trace)
{
    int ret = 0;
    ssize_t index;
    size_t count = 0;

    while ((index = gpt_find_type_partition(gpt, type_guid)) >= 0)
    {
        if (trace)
        {
            size_t partition = count + index + 1;
            printf("%sDeleting partition: %zu%s\n",
                colors_green, partition, colors_reset);
        }

        ECHECK(gpt_remove_partition(gpt, index));
        count++;
    }

    if (trace && count == 0)
    {
        printf("%sDisk contains no partitions of this type%s\n",
            colors_green, colors_reset);
    }

done:
    return ret;
}

int gpt_open(const char* pathname, int flags, gpt_t** gpt_out)
{
    int ret = 0;
    blockdev_t* blockdev = NULL;
    gpt_t* gpt = NULL;

    // Open the block device.
    ECHECK(blockdev_open(pathname, flags, 0, GPT_BLOCK_SIZE, &blockdev));

    // Create the new GPT instance.
    {
        if (!(gpt = calloc(1, sizeof(gpt_t))))
            ERAISE(-ENOMEM);

        gpt->_blockdev = blockdev;
        gpt->_openflags = flags;
        blockdev = NULL;
    }

    // Load the GUID partition table.
    {
        ECHECK(_gpt_load(gpt));
        *gpt_out = gpt;
        gpt = NULL;
    }

done:

    if (blockdev)
        blockdev_close(blockdev);

    if (gpt)
        free(gpt);

    return ret;
}

void gpt_close(gpt_t* gpt)
{
    if (gpt)
    {
        blockdev_close(gpt->_blockdev);

#if 0
        if (gpt->_openflags != O_RDONLY)
            sync();
#endif

        free(gpt);
    }
}

int gpt_get_entry(const gpt_t* gpt, size_t index, gpt_entry_t* entry)
{
    int ret = 0;

    if (index >= gpt->_num_entries)
        ERAISE(-ERANGE);

    *entry = gpt->_primary.entries[index];

done:
    return ret;
}

ssize_t gpt_shrink_partition(gpt_t* gpt, size_t index, size_t num_sectors)
{
    int ret = 0;
    gpt_entry_t entry;
    uint64_t new_ending_lba;

    // Fail if the primary GPT has not been loaded.
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    // Check whether the index it out of range.
    if (index >= gpt->_num_entries)
        ERAISE(-ERANGE);

    ECHECK(gpt_sync(gpt));

    // Get the entry.
    entry = gpt->_primary.entries[index];

    // Adjust the number of sectors to GPT alignment requirement.
#if 1
    num_sectors = (num_sectors + 2047) / 2048 * 2048;
#endif

    // Calculate the new ending LBA.
    new_ending_lba = entry.starting_lba + num_sectors - 1;

    // Fail if requested size if bigger han current size:
    if (new_ending_lba > entry.ending_lba)
        ERAISE(-ERANGE);

    /* Update the entry */
    entry.ending_lba = new_ending_lba;
    gpt->_primary.entries[index] = entry;
    gpt->_backup.entries[index] = entry;

    /* Update the header CRCs */
    ECHECK(_gpt_update_crcs(gpt));

    /* sync changes to device */
    ECHECK(gpt_sync(gpt));

done:
    return ret;
}

ssize_t gpt_resize_partition(gpt_t* gpt, size_t index, size_t num_sectors)
{
    ssize_t ret = 0;
    gpt_entry_t entry;
    uint64_t new_ending_lba;
    size_t max_usable_lba;

    // Fail if the primary GPT has not been loaded.
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    // Check whether the index it out of range.
    if (index >= gpt->_num_entries)
        ERAISE(-ERANGE);

    ECHECK(gpt_sync(gpt));

    // Get the entry.
    entry = gpt->_primary.entries[index];

    // Determine the maximum usable lba for this partition:
    if (index + 1 == gpt->_num_entries)
        max_usable_lba = gpt->_primary.header.last_usable_lba;
    else
        max_usable_lba = gpt->_primary.entries[index+1].starting_lba - 1;

    // Calculate the new ending LBA.
    if (num_sectors == 0)
        new_ending_lba = max_usable_lba;
    else
        new_ending_lba = entry.starting_lba + num_sectors - 1;

    // Fail if requested size is bigger than available space:
    if (new_ending_lba > max_usable_lba)
        ERAISE(-ERANGE);

    // Adjust new_ending_lba so that partition size is multiple of 4096:
    {
        size_t num_sectors = new_ending_lba - entry.starting_lba + 1;

        if (num_sectors % 8)
        {
            num_sectors -= (num_sectors % 8);
            new_ending_lba = entry.starting_lba + num_sectors - 1;
        }
    }

    // Fail if sector is now smaller:
    if (new_ending_lba < entry.ending_lba)
        ERAISE(-ERANGE);

    /* Update the entry */
    entry.ending_lba = new_ending_lba;
    gpt->_primary.entries[index] = entry;
    gpt->_backup.entries[index] = entry;

    /* Update the header CRCs */
    ECHECK(_gpt_update_crcs(gpt));

    /* sync changes to device */
    ECHECK(gpt_sync(gpt));

    ret = (entry.ending_lba - entry.starting_lba) + 1;

done:
    return ret;
}

ssize_t gpt_trailing_free_space(const gpt_t* gpt)
{
    ssize_t ret = 0;

    // Fail if the primary GPT has not been loaded.
    if (!_check_signature(&gpt->_primary.header))
        ERAISE(-EINVAL);

    if (gpt->_num_entries == GPT_MAX_ENTRIES)
        ERAISE(-ERANGE);

    // Find the gap between the last partition and the backup GPT.
    {
        size_t max_ending_lba = gpt->_primary.header.first_usable_lba;

        // Find the maximum ending_lba of any partition.
        for (size_t i = 0; i <  gpt->_num_entries; i++)
        {
            const gpt_entry_t* e =  &gpt->_primary.entries[i];

            if (e->ending_lba > max_ending_lba)
                max_ending_lba = e->ending_lba;
        }

        // Sanity check!
        assert(max_ending_lba <= gpt->_primary.header.last_usable_lba);

        size_t gap = gpt->_primary.header.last_usable_lba - max_ending_lba;
        ret = gap * GPT_SECTOR_SIZE;
    }

done:
    return ret;
}

__attribute__((format(printf, 3, 4)))
static void _print(
    void (*colorize)(int),
    const char* name,
    const char* fmt,
    ...)
{
    colorize(1);
    printf("%s", name);
    colorize(0);
    printf("=");

    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);

        colorize(2);
#if 0
        // calling vprintf() creates stdout dependency!
        vprintf(fmt, ap);
#else
        vsnprintf(buf, sizeof(buf), fmt, ap);
        printf("%s", buf);
#endif

        colorize(0);
        va_end(ap);
    }
}

void gpt_header_dump(
    const gpt_header_t* h,
    bool concise,
    void (*colorize)(int))
{
    const char* s = h->signature;
    guid_t unique_guid;
    guid_string_t unique_guid_str;

    guid_init_xy(&unique_guid, h->unique_guid1, h->unique_guid2);
    guid_format(&unique_guid_str, &unique_guid);

    if (concise)
    {
        printf("[");
        _print(colorize, "sig", "\"%c%c%c%c%c%c%c%c\" ",
            s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
        _print(colorize, "revision", "%u ", h->revision);
        _print(colorize, "header_size", "%u ", h->header_size);
        _print(colorize, "header_crc32", "%u ", h->header_crc32);
        _print(colorize, "primary_lba", "%lu ", h->primary_lba);
        _print(colorize, "backup_lba", "%lu ", h->backup_lba);
        _print(colorize, "first_usable_lba", "%lu ", h->first_usable_lba);
        _print(colorize, "last_usable_lba", "%lu ", h->last_usable_lba);
        _print(colorize, "unique_guid_str", "%s ", unique_guid_str.buf);
        _print(colorize, "first_entry_lba", "%lu ", h->first_entry_lba);
        _print(colorize, "number_of_entries", "%u ", h->number_of_entries);
        _print(colorize, "size_of_entry", "%u ", h->size_of_entry);
        _print(colorize, "entries_crc32", "%u", h->entries_crc32);
        printf("]\n");
    }
    else
    {
        printf("signature=%c%c%c%c%c%c%c%c\n",
            s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
        printf("revision=%u\n", h->revision);
        printf("header_size=%u\n", h->header_size);
        printf("header_crc32=%u\n", h->header_crc32);
        printf("reserved=%u\n", h->reserved);
        printf("primary_lba=%lu\n", h->primary_lba);
        printf("backup_lba=%lu\n", h->backup_lba);
        printf("first_usable_lba=%lu\n", h->first_usable_lba);
        printf("last_usable_lba=%lu\n", h->last_usable_lba);
        printf("unique_guid=%s\n", unique_guid_str.buf);
        printf("first_entry_lba=%lu\n", h->first_entry_lba);
        printf("number_of_entries=%u\n", h->number_of_entries);
        printf("size_of_entry=%u\n", h->size_of_entry);
        printf("entries_crc32=%u\n", h->entries_crc32);
    }
}

void gpt_entry_dump(
    const gpt_entry_t* e,
    bool concise,
    void (*colorize)(int))
{
    guid_t type_guid;
    guid_string_t type_guid_str;
    guid_t unique_guid;
    guid_string_t unique_guid_str;
    char type_name[GPT_ENTRY_TYPENAME_SIZE];
    size_t i;

    guid_init_xy(&type_guid, e->type_guid1, e->type_guid2);
    guid_format(&type_guid_str, &type_guid);

    guid_init_xy(&unique_guid, e->unique_guid1, e->unique_guid2);
    guid_format(&unique_guid_str, &unique_guid);

    for (i = 0; i < GPT_ENTRY_TYPENAME_SIZE && e->type_name[i]; i++)
        type_name[i] = e->type_name[i];
    type_name[i] = '\0';

    if (concise)
    {
        printf("[");
        _print(colorize, "type_guid", "%s ", type_guid_str.buf);
        _print(colorize, "unique_guid", "%s ", unique_guid_str.buf);
        _print(colorize, "starting_lba", "%lu ", e->starting_lba);
        _print(colorize, "ending_lba", "%lu ", e->ending_lba);
        _print(colorize, "attributes", "%lu ", e->attributes);
        _print(colorize, "type_name", "\"%s\"", type_name);
        printf("]\n");
    }
    else
    {
        printf("type_guid=%s\n", type_guid_str.buf);
        printf("unique_guid=%s\n", unique_guid_str.buf);
        printf("starting_lba=%lu\n", e->starting_lba);
        printf("ending_lba=%lu\n", e->ending_lba);
        printf("attributes=%lu\n", e->attributes);
        printf("type_name=\"%s\"", type_name);
    }
}

/* return zero-based partition index */
int find_gpt_entry_by_type(
    const char* disk,
    const guid_t* type,
    char part[PATH_MAX],
    gpt_entry_t* entry)
{
    int ret = 0;
    int32_t index;
    gpt_t* gpt = NULL;

    // Attempt to open the GPT.
    if (gpt_open(disk, O_RDONLY, &gpt) < 0)
        ERAISE(-EINVAL);

    if ((index = gpt_find_type_partition(gpt, type)) < 0)
        ERAISE(-ENOENT);

    if (entry)
        gpt_get_entry(gpt, index, entry);


    if (part)
    {
        uint32_t loopnum;

        if (loop_parse(disk, &loopnum, NULL) < 0)
            ERAISE(-EINVAL);

        loop_format(part, loopnum, index + 1);
    }

    ret = index;

done:

    if (gpt)
        gpt_close(gpt);

    return ret;
}

int gpt_is_sorted(const gpt_t* gpt)
{
    int ret = 0;
    bool found_null_entry = false;

    for (size_t i = 0; i < GPT_MAX_ENTRIES; i++)
    {
        const gpt_entry_t* e = &gpt->_primary.entries[i];

        if (found_null_entry && !_entry_is_null(e))
        {
            // Found null gap!
            ret = -EINVAL;
            goto done;
        }

        if (_entry_is_null(e))
            found_null_entry = true;
    }

done:
    return ret;
}
