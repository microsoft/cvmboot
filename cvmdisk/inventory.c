// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "inventory.h"
#include <common/err.h>
#include <limits.h>
#include <sys/stat.h>
#include <utils/sha256.h>
#include <stdio.h>
#include <unistd.h>
#include "mount.h"
#include "find.h"
#include "sha256.h"
#include "colors.h"

static void _find_files_and_hashes(
    const char* dirname,
    strarr_t* names,
    strarr_t* hashes,
    str_hash_tbl_t* tbl)
{
    if (find(".", names) < 0)
        ERR("find() failed");

    for (size_t i = 0; i < names->size; i++)
    {
        const char* path = names->data[i];
        struct stat statbuf;
        sha256_t hash = SHA256_INITIALIZER;
        sha256_string_t str;

        if (lstat(path, &statbuf) < 0)
            ERR("cannot stat file: %s", path);

        /* only compute hashes for regular files */
        if (S_ISREG(statbuf.st_mode))
        {
            if (sha256_compute_file_hash(&hash, path) < 0)
                ERR("failed to compute hash of file: %s", path);
        }

        sha256_format(&str, &hash);

        if (strarr_append(hashes, str.buf) < 0)
            ERR("out of memory");

        //printf("%s %s\n", path, str.buf);
        str_hash_tbl_insert(tbl, path, hashes->data[hashes->size-1]);
    }

    if (names->size != hashes->size)
        ERR("unexpected");

    if (names->size != tbl->size)
        ERR("unexpected");
}

void get_inventory_snapshot(const char* disk, inventory_t* inventory)
{
    char cwd[PATH_MAX];

    str_hash_tbl_init(&inventory->tbl);

    if (access(disk, F_OK) != 0)
        ERR("cannot access %s", disk);

    mount_disk_ex(disk, MS_RDONLY, false);

    if (!getcwd(cwd, sizeof(cwd)))
        ERR("failed to get the current working directory");

    if (chdir(mntdir()) < 0)
        ERR("failed to change directory to %s", mntdir());

    _find_files_and_hashes(
        ".", &inventory->names, &inventory->hashes, &inventory->tbl);

    if (chdir(cwd) < 0)
        ERR("failed to change directory to %s", cwd);

    umount_disk();
}

void inventory_init(inventory_t* inventory)
{
    strarr_init(&inventory->hashes);
    strarr_init(&inventory->names);
    str_hash_tbl_init(&inventory->tbl);
}

void inventory_release(inventory_t* inventory)
{
    str_hash_tbl_release(&inventory->tbl, NULL);
    strarr_release(&inventory->hashes);
    strarr_release(&inventory->names);
}

void print_inventory_delta(const inventory_t* inv1, const inventory_t* inv2)
{
    /* print out added and modified files */
    for (size_t i = 0; i < inv2->names.size; i++)
    {
        const char* path = inv2->names.data[i];
        const char* hash = inv2->hashes.data[i];
        void* value = NULL;

        if (str_hash_tbl_find(&inv1->tbl, path, &value) < 0)
        {
            printf("%snew file: %s%s\n", colors_green, path, colors_reset);
        }
        else if (strcmp(hash, (const char*)value) != 0)
        {
            printf("%smodified: %s%s\n", colors_red, path, colors_reset);
        }
    }

    /* print out deleted files */
    for (size_t i = 0; i < inv1->names.size; i++)
    {
        const char* path = inv1->names.data[i];
        void* value = NULL;

        if (str_hash_tbl_find(&inv2->tbl, path, &value) < 0)
        {
            printf("%sdeleted:  %s%s\n", colors_cyan, path, colors_reset);
        }
    }
}
