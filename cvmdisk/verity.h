// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_VERITY_H
#define _CVMBOOT_CVMDISK_VERITY_H

#include <utils/sha256.h>
#include <utils/err.h>
#include <stdint.h>
#include <stddef.h>
#include <common/err.h>
#include "blockdev.h"
#include "defs.h"
#include "guid.h"

#define VERITY_SUPERBLOCK_SIZE 512
#define VERITY_SIGNATURE "verity\0"
#define VERITY_SIGNATURE_SIZE sizeof(VERITY_SIGNATURE)
#define VERITY_MAX_SALT_SIZE 256
#define VERITY_ROOTHASH_SIZE SHA256_SIZE
#define VERITY_BLOCK_SIZE 4096

typedef struct verity_superblock
{
    /* (0) "verity\0\0" */
    uint8_t signature[8];

    /* (8) superblock version, 1 */
    uint32_t version;

    /* (12) 0 - Chrome OS, 1 - normal */
    uint32_t hash_type;

    /* (16) UUID of hash device */
    uint8_t uuid[16];

    /* (32) Name of the hash algorithm (e.g., sha256) */
    char algorithm[32];

    /* (64) The data block size in bytes */
    uint32_t data_block_size;

    /* (68) The hash block size in bytes */
    uint32_t hash_block_size;

    /* (72) The number of data blocks */
    uint64_t data_blocks;

    /* (80) Size of the salt */
    uint16_t salt_size;

    /* (82) Padding */
    uint8_t _pad1[6];

    /* (88) The salt */
    uint8_t salt[VERITY_MAX_SALT_SIZE];

    /* Padding */
    uint8_t _pad2[168];
}
__attribute__((packed))
verity_superblock_t;

typedef struct verity_hashtree
{
    uint8_t* data;
    size_t size;
    const uint8_t* leaves_start;
    const uint8_t* leaves_end;
}
verity_hashtree_t;

void verity_superblock_dump(const verity_superblock_t* sb);

#define VERITY_PARTITION_TYPE_GUID "3416e185-0efa-4ba5-bf43-be206e7f9af0"

#define VERITY_MIN_HASH_DEV_SIZE 4096

typedef struct
{
    uint8_t data[VERITY_BLOCK_SIZE];
}
verity_block_t;

int verity_add_partition(
    const char* disk,
    const char* data_dev_path,
    bool trace,
    bool progress,
    guid_t* unique_guid,
    sha256_t* roothash,
    err_t* err);

int verity_format(
    blockdev_t* data_dev,
    blockdev_t* hash_dev,
    const guid_t* verity_uuid,
    sha256_t* root_hash,
    bool trace,
    bool progress);

int verity_get_roothash(blockdev_t* hash_dev, sha256_t* roothash);

int verity_get_superblock(blockdev_t* hash_dev, verity_superblock_t* sb);

void verity_dump_superblock(const verity_superblock_t* sb);

// For the given data device size, calculate the size of the hash device.
// Return the size in bytes.
ssize_t verity_hash_dev_size(size_t data_dev_size);

int verity_load_hash_tree(
    blockdev_t* dev,
    verity_superblock_t* sb,
    const sha256_t* roothash,
    verity_hashtree_t* hashtree);

int verity_verify_data_device(
    blockdev_t* dev,
    verity_superblock_t* sb,
    const sha256_t* roothash,
    verity_hashtree_t* hashtree);

#endif /* _CVMBOOT_CVMDISK_VERITY_H */
