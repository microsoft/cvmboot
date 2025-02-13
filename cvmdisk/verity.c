// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <assert.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <utils/hexstr.h>
#include <utils/sha256.h>
#include "eraise.h"
#include "verity.h"
#include "gpt.h"
#include "blockdev.h"
#include "loop.h"
#include "colors.h"
#include "guid.h"
#include "progress.h"
#include "frags.h"
#include "globals.h"
#include "bits.h"
#include "round.h"

#define USE_ZERO_BLOCK_OPTIMIZATION
#define USE_SPARSE_VERITY_FORMATTING
#define VERITY_SIGNATURE "verity\0"
#define VERITY_SIGNATURE_SIZE sizeof(VERITY_SIGNATURE)
#define MAX_ROOTHASH_SIZE 256

#define USE_ZERO_SALT

static_assert(VERITY_SIGNATURE_SIZE == 8);
static_assert(sizeof(verity_superblock_t) == 512);

static uint8_t _zeros[VERITY_BLOCK_SIZE];

/* ATTN: use similar function in round.h */
static __inline__ uint64_t _round_up(uint64_t x, uint64_t m)
{
    return (x + m - 1) / m * m;
}

/* ATTN: use similar function in round.h */
static size_t _next_multiple(size_t x, size_t m)
{
    return (x + m - 1) / m;
}

#ifdef USE_ZERO_BLOCK_OPTIMIZATION
static bool _all_zeros_128(const void* s, size_t n)
{
    size_t r = n / sizeof(__uint128_t);
    __uint128_t* p = (__uint128_t*)s;

    while (r > 0 && *p == 0)
    {
        p++;
        r--;
    }

    return r == 0;
}
#endif /* USE_ZERO_BLOCK_OPTIMIZATION */

void verity_superblock_dump(const verity_superblock_t* sb)
{
    guid_string_t guid_str;
    guid_t guid;

    guid_init_bytes(&guid, sb->uuid);
    guid_format(&guid_str, &guid);

    printf("=== verity superblock\n");
    printf("signature=\"%s\"\n", sb->signature);
    printf("version=%u\n", sb->version);
    printf("hash_type=%u\n", sb->hash_type);
    printf("uuid=%s\n", guid_str.buf);
    printf("algorithm=\"%s\"\n", sb->algorithm);
    printf("data_block_size=%u\n", sb->data_block_size);
    printf("hash_block_size=%u\n", sb->hash_block_size);
    printf("data_blocks=%lu\n", sb->data_blocks);
    printf("salt_size=%u\n", sb->salt_size);
    printf("salt=");
    hexstr_dump(sb->salt, sb->salt_size);
}

int verity_add_partition(
    const char* disk,
    const char* data_dev_path,
    bool trace,
    bool progress,
    guid_t* unique_guid,
    sha256_t* roothash,
    err_t* err)
{
    int ret = 0;
    size_t hash_dev_size = 0;
    blockdev_t* data_dev = NULL;
    blockdev_t* hash_dev = NULL;
    gpt_t* gpt = NULL;
    int r;
    guid_t verity_uuid;
    ssize_t data_dev_size;
    char hash_dev_path[PATH_MAX] = "";

    // Clear all output parameters
    guid_clear(unique_guid);
    sha256_clear(roothash);
    err_clear(err);

    // Get the size of the data device
    if ((data_dev_size = blockdev_getsize64(data_dev_path)) < 0)
        ERR("cannot get size of %s", data_dev_path);

    // Calculate the size of the hash device (from the data device)
    if ((hash_dev_size = verity_hash_dev_size(data_dev_size)) < 0)
    {
        err_format(err, "failed to get the hash device size");
        ERAISE(hash_dev_size);
    }

    if (trace)
    {
        printf("%s>>> Adding verity partition for %s...%s\n",
            colors_green, data_dev_path, colors_reset);
    }

    // Attempt to open the GPT.
    if ((r = gpt_open(disk, O_RDWR | O_EXCL, &gpt)) < 0)
    {
        err_format(err, "GUID partition table not found: %s", disk);
        ERAISE(r);
    }

    // Add a new verity-hash-device partition.
    {
        guid_t type_guid;
        guid_init_str(&type_guid, VERITY_PARTITION_TYPE_GUID);
        size_t num_blocks = hash_dev_size / GPT_BLOCK_SIZE;
        uint64_t attributes = 0;
        uint16_t type_name[GPT_ENTRY_TYPENAME_SIZE] =
            { 'V', 'E', 'R', 'I', 'T', 'Y', '\0', '\0' };

        // Generate the unique partition GUID.
        guid_generate(unique_guid);

        if ((r = gpt_add_partition(
            gpt,
            &type_guid,
            unique_guid,
            num_blocks,
            attributes,
            type_name)) < 0)
        {
            err_format(err, "failed to add partition");
            ERAISE(r);
        }

        // Synchronize the GPT.
        ECHECK(gpt_sync(gpt));
    }

    // Get the GPT entry for the hash device
    {
        size_t index = 0;
        uint32_t loopnum;

        // Find the next available partition index.
        if ((index = gpt_find_partition(gpt, unique_guid)) == (size_t)-1)
        {
            err_format(err, "unexpected: failed to find partition");
            ERAISE(index);
        }

        if (loop_parse(disk, &loopnum, NULL) < 0)
        {
            err_format(err, "failed to loop device: %s", disk);
            ERAISE(-EINVAL);
        }

        loop_format(hash_dev_path, loopnum, index + 1);
    }

    if (trace)
        printf("Created verity partition");

    // Set the verity-uuid to the unique-guid of the data device.
    {
        uint32_t loopnum;
        uint32_t partnum;
        gpt_entry_t e;

        if (loop_parse(data_dev_path, &loopnum, &partnum) != 0)
        {
            err_format(err, "cannot parse SCSI device path: %s", data_dev_path);
            ERAISE(-EINVAL);
        }

        if (partnum == 0)
        {
            err_format(err, "invalid data device pathname: %s", data_dev_path);
            ERAISE(-EINVAL);
        }

        if ((r = gpt_get_entry(gpt, partnum - 1, &e)) < 0)
        {
            err_format(err, "cannot find GPT entry for partition %u", partnum);
            ERAISE(-ENOENT);
        }

        guid_init_xy(&verity_uuid, e.unique_guid1, e.unique_guid2);
    }

    // Close the GPT:
    gpt_close(gpt);
    gpt = NULL;

    // Open the data device for read.
    if ((r = blockdev_open(
        data_dev_path,
        O_RDONLY,
        0,
        VERITY_BLOCK_SIZE,
        &data_dev)) < 0)
    {
        err_format(err, "failed to open data device: %s", globals.disk);
        ERAISE(r);
    }

    // Open the hash device (the new partition).
    if ((r = blockdev_open(
        hash_dev_path,
        O_RDWR,
        0600,
        VERITY_BLOCK_SIZE,
        &hash_dev)) < 0)
    {
        err_format(err, "failed to open hash device: %s", globals.disk);
        ERAISE(r);
    }

    // Format the new partition.
    if ((r = verity_format(
        data_dev, hash_dev, &verity_uuid, roothash, trace, progress)) < 0)
    {
        err_format(err, "failed to format hash device");
        ERAISE(r);
    }

    if (trace)
    {
        sha256_string_t str;
        sha256_format(&str, roothash);
        printf("roothash: %s\n", str.buf);
    }

    if (data_dev)
    {
        blockdev_close(data_dev);
        data_dev = NULL;
    }

    if (hash_dev)
    {
        blockdev_close(hash_dev);
        hash_dev = NULL;
    }

done:

    if (data_dev)
        blockdev_close(data_dev);

    if (hash_dev)
        blockdev_close(hash_dev);

    if (gpt)
        gpt_close(gpt);

    return ret;
}

/* Fill the hash device with zero blocks */
/* ATTN: consider removing unused function */
__attribute__((__unused__))
static int _zero_fill_hash_device(
    blockdev_t* hash_dev,
    size_t total_nodes,
    bool need_superblock,
    bool print_progress)
{
    int ret = 0;
    size_t num_blocks = total_nodes;
    size_t min_num_blocks = VERITY_MIN_HASH_DEV_SIZE / VERITY_BLOCK_SIZE;
    const char msg[] = "Zero-filling the verity partition";
    progress_t progress;

    if (need_superblock)
        num_blocks++;

    if (num_blocks < min_num_blocks)
        num_blocks = min_num_blocks;

    if (print_progress)
        progress_start(&progress, msg);

    for (size_t i = 0; i < num_blocks; i++)
    {
        ECHECK(blockdev_put(hash_dev, i, _zeros, 1));
        progress_update(&progress, i, num_blocks);
    }

    progress_end(&progress);

done:
    return ret;
}

static int _create_rootfs_sparse_bit_string(
    const char* disk,
    size_t* rootfs_block_offset_out,
    uint8_t** non_sparse_bits_out,
    size_t* non_sparse_bits_size_out)
{
    int ret = 0;
    const size_t blksz = VERITY_BLOCK_SIZE;
    gpt_entry_t entry;
    frag_list_t frags = FRAG_LIST_INITIALIZER;
    frag_list_t holes = FRAG_LIST_INITIALIZER;
    size_t offset;
    size_t end;
    const guid_t guid = linux_type_guid;
    size_t rootfs_block_offset = 0;
    uint8_t* non_sparse_bits = NULL;
    size_t non_sparse_bits_size = 0;

    /* Find GPT entry of the rootfs partition (first Linux partition) */
    if ((find_gpt_entry_by_type(disk, &guid, NULL, &entry)) < 0)
        ERAISE(-EINVAL);

    /* Calculate offset/end of rootfs partition */
    offset = gpt_entry_offset(&entry);
    end = offset + gpt_entry_size(&entry);

    /* Find the non-sparse and sparse fragments */
    if (frags_find(disk, offset, end, &frags, &holes) < 0)
        ERAISE(-EINVAL);

    /* Check alignment of offset and end */
    if (offset % blksz || end % blksz)
    {
        fprintf(stderr, "Unexpected partition alignment error\n");
        ERAISE(-EINVAL);
    }

    /* Only apply sparse optimization if there are holes */
    if (holes.num_blocks > 0)
    {
        size_t nbits = round_up_to_multiple(((end + blksz) / blksz), 8);
        non_sparse_bits_size = nbits / 8;
        rootfs_block_offset = offset / blksz;

        /* Allocate bit string for the sparse and non-sparse blocks */
        if (!(non_sparse_bits = calloc(1, non_sparse_bits_size)))
            ERAISE(-ENOMEM);

        /* Set bits corresponding to non-sparse blocks */
        frags_set_bits(&frags, non_sparse_bits, non_sparse_bits_size);
    }

    frags_release(&frags);
    frags_release(&holes);

    *rootfs_block_offset_out = rootfs_block_offset;
    *non_sparse_bits_out = non_sparse_bits;
    *non_sparse_bits_size_out = non_sparse_bits_size;

done:
    return ret;
}

int verity_format(
    blockdev_t* data_dev,
    blockdev_t* hash_dev,
    const guid_t* verity_uuid,
    sha256_t* roothash,
    bool trace,
    bool print_progress)
{
    int ret = 0;
    size_t nblks;
    size_t digests_per_blk;
    const size_t hsize = sizeof(sha256_t);
    uint8_t salt[VERITY_MAX_SALT_SIZE];
    const size_t salt_size = hsize;
    size_t nleaves;
    size_t nnodes[32];
    size_t levels = 0;
    size_t total_nodes = 0;
    uint64_t data_block_size = VERITY_BLOCK_SIZE;
    uint64_t hash_block_size = VERITY_BLOCK_SIZE;
    const size_t blksz = data_block_size;
    const size_t min_data_file_size = blksz * 2;
    uint8_t last_node[blksz];
    const char hash_name[] = "sha256";
    const bool need_superblock = true;
    uint8_t zeros[blksz];
    sha256_t zero_hash = SHA256_INITIALIZER;
    const char msg[] = "Formatting verity partition";
    uint8_t* block_checklist = NULL;
    progress_t progress;
    uint64_t rootfs_block_offset = 0;
    uint8_t* non_sparse_bits = NULL;
    size_t non_sparse_bits_size = 0;

    memset(zeros, 0, blksz);

    if (!data_dev || !hash_dev)
        ERAISE(-EINVAL);

    if (data_dev->block_size != data_block_size)
        ERAISE(-EINVAL);

    if (hash_dev->block_size != hash_block_size)
        ERAISE(-EINVAL);

    memset(salt, 0, sizeof(salt));

#ifndef USE_ZERO_SALT
    if (getrandom(salt, salt_size, 0) != salt_size)
        ERAISE(-ERANGE);
#endif

    // Precalculate the hash of the zero block.
    sha256_compute2(&zero_hash, salt, salt_size, zeros, blksz);

    // Calculate the number of data blocks.
    {
        const size_t size = data_dev->file_size;

        /* File must be a multiple of the block size */
        if (size % blksz)
            ERAISE(-ERANGE);

        if (size < min_data_file_size)
            ERAISE(-ERANGE);

        nblks = size / blksz;
    }

    /* Calculate the number of digests per blocks */
    digests_per_blk = blksz / hsize;

    /* Calculate the number of leaf nodes */
    nleaves = _round_up(nblks, digests_per_blk) / digests_per_blk;
    nnodes[levels++] = nleaves;

    /* Calculate the number of interior nodes at each levels */
    {
        /* Save the nodes at the leaf levels */
        size_t n = nleaves;

        while (n > 1)
        {
            n = _round_up(n, digests_per_blk) / digests_per_blk;
            nnodes[levels++] = n;
        }
    }

    /* Calculate the total number of nodes at all levels */
    for (size_t i = 0; i < levels; i++)
        total_nodes += nnodes[i];

    /* Create a block check list */
    const size_t num_hash_blocks = hash_dev->file_size / hash_dev->block_size;

    if (!(block_checklist = calloc(num_hash_blocks, 1)))
        ERAISE(-EINVAL);

#ifdef USE_SPARSE_VERITY_FORMATTING
    // Construct a bit string and set the bits that correspond to the
    // non-sparse blocks of rootfs partition
    {
        ECHECK(_create_rootfs_sparse_bit_string(
            globals.disk,
            &rootfs_block_offset,
            &non_sparse_bits,
            &non_sparse_bits_size));
    }
#endif /* USE_SPARSE_VERITY_FORMATTING */

#ifdef ENABLE_ZERO_FILL
    ECHECK(_zero_fill_hash_device(
        hash_dev, total_nodes, need_superblock, print_progress));
#endif

    /* Write the leaf nodes */
    {
        __attribute__((aligned(16))) uint8_t blk[blksz];
        uint8_t node[blksz];
        size_t node_offset = 0;
        size_t offset;
        size_t nblocks = data_dev->file_size / data_block_size;

        /* Calculate the hash file offset to the first leaf node block */
        offset = (total_nodes - nleaves) * blksz;

        if (need_superblock)
            offset += blksz;

        /* Zero out the node */
        memset(node, 0, blksz);

        progress_start(&progress, msg);

        /* For each block in the file */
        for (size_t i = 0; i < nblocks; i++)
        {
            sha256_t h = SHA256_INITIALIZER;
            bool is_sparse_block = false;

            progress_update(&progress, i, nblocks);

            /* If using sparse optimization */
            if (non_sparse_bits &&
                !test_bit(non_sparse_bits, i + rootfs_block_offset))
            {
                is_sparse_block = true;
            }
            else
            {
                ECHECK(blockdev_get(data_dev, i, blk, 1));
            }

            /* Compute the hash of the current block */
#ifdef USE_ZERO_BLOCK_OPTIMIZATION
            if (is_sparse_block || _all_zeros_128(blk, blksz))
                h = zero_hash;
            else
                sha256_compute2(&h, salt, salt_size, blk, blksz);
#else
            sha256::compute2(salt, salt_size, blk, blksz, h);
#endif

            /* Write out the node if full */
            if (node_offset + hsize > blksz)
            {
                assert((offset % blksz) == 0);
                const size_t blkno = offset / blksz;
                ECHECK(blockdev_put(hash_dev, blkno, node, 1));
                block_checklist[blkno] = 1;
                memcpy(last_node, node, blksz);
                offset += blksz;
                memset(node, 0, blksz);
                node_offset = 0;
            }

            memcpy(node + node_offset, h.data, hsize);
            node_offset += hsize;
        }

        /* Write the final hash file block if any */
        if (node_offset > 0)
        {
            assert((offset % blksz) == 0);
            const size_t blkno = offset / blksz;
            ECHECK(blockdev_put(hash_dev, blkno, node, 1));
            block_checklist[blkno] = 1;
            memcpy(last_node, node, blksz);
        }
    }

    /* Write the interior nodes */
    for (size_t i = 1; i < levels; i++)
    {
        size_t write_offset = 0;
        size_t read_offset = 0;
        size_t num_to_read;
        size_t num_to_write;

        /* Compute the hash file read offset */
        for (size_t j = i; j < levels; j++)
            read_offset += nnodes[j] * blksz;

        if (need_superblock)
            read_offset += blksz;

        /* Compute the hash file write offset */
        for (size_t j = i + 1; j < levels; j++)
            write_offset += nnodes[j] * blksz;

        if (need_superblock)
            write_offset += blksz;

        num_to_read = nnodes[i-1];
        num_to_write = nnodes[i];

        /* For each interior node at this level */
        for (size_t j = 0; j < num_to_write; j++)
        {
            uint8_t node[blksz];
            size_t node_offset = 0;

            /* Zero out the next interior node */
            memset(node, 0, blksz);

            /* Fill the interior node with hashes */
            while (num_to_read && (node_offset + hsize) <= blksz)
            {
                char blk[blksz];
                sha256_t h = SHA256_INITIALIZER;

                /* Read the next block */
                {
                    assert((read_offset % blksz) == 0);
                    const size_t blkno = read_offset / blksz;

                    /* write zero block if not yet written */
                    if (!block_checklist[blkno])
                        ECHECK(blockdev_put(hash_dev, blkno, zeros, 1));

                    ECHECK(blockdev_get(hash_dev, blkno, blk, 1));
                    read_offset += blksz;
                }

                /* Compute the hash of this block */
                sha256_compute2(&h, salt, salt_size, blk, blksz);

                /* Copy this hash to the new node */
                memcpy(node + node_offset, h.data, hsize);
                node_offset += hsize;

                num_to_read--;
            }

            /* Write out this interior node */
            {
                assert((write_offset % blksz) == 0);
                const size_t blkno = write_offset / blksz;
                ECHECK(blockdev_put(hash_dev, blkno, node, 1));
                block_checklist[blkno] = 1;
                memcpy(last_node, node, blksz);
                write_offset += blksz;
            }
        }
    }

    /* Compute the root hash (from the last block written) */
    {
        sha256_t h = SHA256_INITIALIZER;
        sha256_compute2(&h, salt, salt_size, last_node, blksz);
        *roothash = h;
    }

    /* Write the superblock */
    {
        verity_superblock_t sb;

        memset(&sb, 0, sizeof(sb));

        memcpy(sb.signature, VERITY_SIGNATURE, 8);
        sb.version = 1;
        sb.hash_type = 1;

        guid_get_bytes(verity_uuid, sb.uuid);

        strcpy(sb.algorithm, hash_name);
        sb.data_block_size = blksz;
        sb.hash_block_size = blksz;
        sb.data_blocks = nblks;
        memcpy(sb.salt, salt, salt_size);
        sb.salt_size = salt_size;

        if (need_superblock)
        {
            uint8_t blk[blksz];

            memset(blk, 0, blksz);
            memcpy(blk, &sb, sizeof(verity_superblock_t));
            ECHECK(blockdev_put(hash_dev, 0, blk, 1));
            block_checklist[0] = 1;
        }
    }

    /* Write zeros to any blocks that were not written above */
    for (size_t i = 0; i < num_hash_blocks; i++)
    {
        if (!block_checklist[i])
            ECHECK(blockdev_put(hash_dev, i, zeros, 1));
    }

    progress_end(&progress);

done:

    if (block_checklist)
        free(block_checklist);

    return ret;
}

ssize_t verity_hash_dev_size(size_t data_dev_size)
{
    ssize_t ret = 0;
    size_t nblks;
    size_t digests_per_blk;
    const size_t hsize = sizeof(sha256_t);
    size_t nleaves;
    size_t nnodes[32];
    size_t levels = 0;
    size_t total_nodes = 0;
    uint64_t data_block_size = VERITY_BLOCK_SIZE;
    const size_t blksz = data_block_size;
    const size_t min_data_file_size = blksz * 2;
    const bool need_superblock = true;
    size_t hash_dev_size = 0;

    // Calculate the number of data blocks.
    {
        /* File must be a multiple of the block size */
        if (data_dev_size % data_block_size)
            ERAISE(-ERANGE);

        if (data_dev_size < min_data_file_size)
            ERAISE(-ERANGE);

        nblks = data_dev_size / data_block_size;
    }

    // Calculate the number of digests per blocks.
    digests_per_blk = blksz / hsize;

    // Calculate the number of leaf nodes.
    nleaves = _round_up(nblks, digests_per_blk) / digests_per_blk;
    nnodes[levels++] = nleaves;

    // Calculate the number of interior nodes at each level.
    {
        /* Save the nodes at the leaf levels */
        size_t n = nleaves;

        while (n > 1)
        {
            n = _round_up(n, digests_per_blk) / digests_per_blk;
            nnodes[levels++] = n;
        }
    }

    // Calculate the total number of nodes at all levels.
    for (size_t i = 0; i < levels; i++)
        total_nodes += nnodes[i];

    // Calculate the size of the hash file.
    {
        size_t nblks = total_nodes;

        if (need_superblock)
            nblks++;

        hash_dev_size = nblks * blksz;

        if (hash_dev_size < VERITY_MIN_HASH_DEV_SIZE)
            hash_dev_size = VERITY_MIN_HASH_DEV_SIZE;
    }

    ret = hash_dev_size;

done:
    return ret;
}

int verity_get_superblock(blockdev_t* hash_dev, verity_superblock_t* sb)
{
    int ret = 0;
    union
    {
        verity_superblock_t sb;
        uint8_t padding[VERITY_BLOCK_SIZE];
    }
    u;

    memset(sb, 0, sizeof(verity_superblock_t));

    // Reject null arguments.
    if (!hash_dev)
        ERAISE(-EINVAL);

    // Read the first block that begins with the superblock.
    ECHECK(blockdev_get(hash_dev, 0, &u, 1));

    // Check the verity superblock signature.
    if (memcmp(u.sb.signature, VERITY_SIGNATURE, VERITY_SIGNATURE_SIZE) != 0)
        ERAISE(-EINVAL);

    // Check the salt size.
    if (u.sb.salt_size != SHA256_SIZE)
        ERAISE(-EINVAL);

    // Check the hash block size.
    if (u.sb.hash_block_size != VERITY_BLOCK_SIZE)
        ERAISE(-EINVAL);

    *sb = u.sb;

done:
    return ret;
}

int verity_get_roothash(blockdev_t* hash_dev, sha256_t* roothash)
{
    int ret = 0;
    union
    {
        verity_superblock_t sb;
        uint8_t padding[VERITY_BLOCK_SIZE];
    }
    u;
    uint8_t hash_block[VERITY_BLOCK_SIZE];

    sha256_clear(roothash);

    // Reject null arguments.
    if (!hash_dev)
        ERAISE(-EINVAL);

    // Read the first block that begins with the superblock.
    ECHECK(blockdev_get(hash_dev, 0, &u, 1));

    // Check the verity superblock signature.
    if (memcmp(u.sb.signature, VERITY_SIGNATURE, VERITY_SIGNATURE_SIZE) != 0)
        ERAISE(-EINVAL);

    // Check the salt size.
    if (u.sb.salt_size != SHA256_SIZE)
        ERAISE(-EINVAL);

    // Check the hash block size.
    if (u.sb.hash_block_size != VERITY_BLOCK_SIZE)
        ERAISE(-EINVAL);

    // Read the first hash block.
    ECHECK(blockdev_get(hash_dev, 1, hash_block, 1));

    // Calculate the roothash.
    sha256_compute2(
        roothash,
        u.sb.salt,
        u.sb.salt_size,
        hash_block,
        u.sb.hash_block_size);

done:
    return ret;
}

void verity_dump_superblock(const verity_superblock_t* sb)
{
    guid_t uuid;
    guid_init_bytes(&uuid, sb->uuid);
    guid_string_t str;
    guid_format(&str, &uuid);

    printf("superblock\n");
    printf("{\n");
    printf("    signature: %s\n", (const char*)sb->signature);
    printf("    version: %u\n", sb->version);
    printf("    hash_type: %u\n", sb->hash_type);
    printf("    uuid: %s\n", str.buf);
    printf("    algorithm: %s\n", sb->algorithm);
    printf("    data_block_size: %u\n", sb->data_block_size);
    printf("    hash_block_size: %u\n", sb->hash_block_size);
    printf("    data_blocks: %lu\n", sb->data_blocks);
    printf("    salt_size: %u\n", sb->salt_size);

    char salt_buf[VERITY_MAX_SALT_SIZE * 2 + 1];
    hexstr_format(salt_buf, sizeof(salt_buf), sb->salt, sb->salt_size);
    printf("    salt: %s\n", salt_buf);

    printf("}\n");
}

int verity_load_hash_tree(
    blockdev_t* dev,
    verity_superblock_t* sb,
    const sha256_t* roothash,
    verity_hashtree_t* hashtree)
{
    int ret = 0;
    const size_t hash_size = sb->salt_size;
    const size_t num_blocks = sb->data_blocks;
    const size_t digests_per_block = sb->hash_block_size / hash_size;
    const size_t blksz = sb->hash_block_size;
    struct level
    {
        size_t nnodes;
        size_t offset;
    };
    size_t nlevels = 0;
    size_t nchecks = 0;
    size_t total_nodes = 0;
    struct level levels[32];

    if (!dev)
        ERAISE(-EINVAL);

    if (sb->hash_block_size != VERITY_BLOCK_SIZE)
        ERAISE(-EINVAL);

    if (sb->data_block_size != VERITY_BLOCK_SIZE)
        ERAISE(-EINVAL);

    if (sb->hash_type != 1)
        ERAISE(-EINVAL);

    if (strcmp(sb->algorithm, "sha256") != 0)
        ERAISE(-EINVAL);

    if (sb->salt_size != SHA256_SIZE)
        ERAISE(-EINVAL);

    /* count the number of nodes at every level of the hash tree */
    {
        size_t n = num_blocks;

        do
        {
            n = _next_multiple(n, digests_per_block);
            levels[nlevels++].nnodes = n;
        } while (n > 1);
    }

    /* calculate the offsets for each level */
    {
        size_t offset = 0;

        for (ssize_t i = (ssize_t)nlevels - 1; i >= 0; i--)
        {
            levels[i].offset = offset;
            offset += levels[i].nnodes;
        }
    }

    /* calculate the total number of nodes in the hash tree */
    for (size_t i = 0; i < nlevels; i++)
    {
        total_nodes += levels[i].nnodes;
#if 0
        printf(
              "levels(index=%zu, nnodes=%zu offset=%zu)\n",
              i,
              levels[i].nnodes,
              levels[i].offset);
#endif
    }

    /* allocate space for all the hash blocks */
    {
        hashtree->size = total_nodes * blksz;

        if (!(hashtree->data = malloc(hashtree->size)))
            ERAISE(-ENOMEM);
    }

    /* read the hash blocks into memory (skip the superblock) */
    for (size_t i = 0; i < total_nodes; i++)
    {
        size_t blkno = i + 1;
        uint8_t* block = hashtree->data + (i * blksz);
        ECHECK(blockdev_get(dev, blkno, block, 1));
    }

    /* save pointer to the start of the hash leaves */
    hashtree->leaves_start = hashtree->data + (levels[0].offset * blksz);
    hashtree->leaves_end = hashtree->data + hashtree->size;

    /* verify the hash tree from the bottom up */
    for (size_t i = 0; i < nlevels; i++)
    {
        const size_t nnodes = levels[i].nnodes;
        const size_t offset = levels[i].offset;
        size_t parent = 0;
        const uint8_t* htree = hashtree->data;
        const uint8_t* phash = NULL;

        /* set pointer to current parent hash */
        if (i + 1 != nlevels)
            phash = htree + (levels[i + 1].offset * blksz);

        for (size_t j = 0; j < nnodes; j++)
        {
            size_t index = j + offset;
            const void* data = htree + (index * blksz);
            sha256_t hash = SHA256_INITIALIZER;

            sha256_compute2(&hash, sb->salt, sb->salt_size, data, blksz);

            /* find parent hash and see if it matched */
            if (phash)
            {
                if (memcmp(phash, &hash, sizeof(sha256_t)) != 0)
                    ERAISE(-EIO);

                phash += sizeof(sha256_t);
            }
            else if (sha256_compare(roothash, &hash) != 0)
            {
                ERAISE(-EIO);
            }

            /* count the number of hash verification checks performed */
            nchecks++;

            if (j > 0 && (j % digests_per_block) == 0)
                parent++;
        }
    }

    if (nchecks != total_nodes)
        ERAISE(-EIO);

done:

    return ret;
}

int verity_verify_data_device(
    blockdev_t* dev,
    verity_superblock_t* sb,
    const sha256_t* roothash,
    verity_hashtree_t* hashtree)
{
    int ret = 0;
    const char msg[] = "Verifying data blocks";
    bool print_progress = true;
    FILE* stream = stdout;
    size_t blksz = VERITY_BLOCK_SIZE;
    __attribute__((aligned(16))) uint8_t blk[blksz];
    uint8_t zeros[blksz];
    sha256_t zero_hash = SHA256_INITIALIZER;
    size_t check_count = 0;
    progress_t progress;
    size_t rootfs_block_offset = 0;
    uint8_t* non_sparse_bits = NULL;
    size_t non_sparse_bits_size = 0;

    if (!dev)
        ERAISE(-EINVAL);

    if (sb->data_blocks != (blockdev_get_size(dev) / blksz))
        ERAISE(-EINVAL);

    // Precalculate the hash of the zero block.
    memset(zeros, 0, blksz);
    sha256_compute2(&zero_hash, sb->salt, sb->salt_size, zeros, blksz);

#ifdef USE_SPARSE_VERITY_FORMATTING
    // Construct a bit string and set the bits that correspond to the
    // non-sparse blocks of rootfs partition
    {
        ECHECK(_create_rootfs_sparse_bit_string(
            globals.disk,
            &rootfs_block_offset,
            &non_sparse_bits,
            &non_sparse_bits_size));
    }
#endif /* USE_SPARSE_VERITY_FORMATTING */

    // Print zero percentage complete.
    if (print_progress)
        progress_start(&progress, msg);

    for (size_t blkno = 0; blkno < sb->data_blocks; blkno++)
    {
        sha256_t hash;
        bool is_sparse_block = false;

        if (print_progress)
            progress_update(&progress, blkno, sb->data_blocks);

        /* If using sparse optimization */
        if (non_sparse_bits &&
            !test_bit(non_sparse_bits, blkno + rootfs_block_offset))
        {
            is_sparse_block = true;
        }
        else
        {
            ECHECK(blockdev_get(dev, blkno, blk, 1));
        }

#ifdef USE_ZERO_BLOCK_OPTIMIZATION
        if (is_sparse_block || _all_zeros_128(blk, blksz))
            hash = zero_hash;
        else
            sha256_compute2(&hash, sb->salt, sb->salt_size, blk, blksz);
#else
        sha256_compute2(&hash, sb.salt, sb.salt_size, blk, blksz);
#endif

        // Check the data block against the hash tree.
        {
            const uint8_t* p = hashtree->leaves_start + blkno * sizeof(hash);

            if (!(p >= hashtree->leaves_start && p < hashtree->leaves_end))
                ERAISE(-ERANGE);

            if (memcmp(&hash, p, sizeof(hash)) != 0)
                ERAISE(-EIO);

            check_count++;
        }
    }

    if (check_count != sb->data_blocks)
        ERAISE(-EIO);

    if (print_progress)
        progress_end(&progress);

done:

    if (ret < 0)
    {
        fprintf(stream, "\n");
        fflush(stream);
    }

    return ret;
}
