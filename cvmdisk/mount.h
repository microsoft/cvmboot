// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_MOUNT_H
#define _CVMBOOT_CVMDISK_MOUNT_H

#include <stdbool.h>
#include <sys/mount.h>
#include <limits.h>
#include <stddef.h>
#include "path.h"

#define MAX_MOUNTPOINTS 8

typedef struct mount_context
{
    /* source for rootfs partition  mount */
    char source[PATH_MAX];

    path_t mountpoints[MAX_MOUNTPOINTS];

    size_t num_mountpoints;

    /* MS_RDONLY */
    int mount_flags;
}
mount_context_t;

void mount_disk_ex(const char* disk, int flags, bool bind);

void mount_disk(const char* disk, int flags);

void umount_disk(void);

const char* mntdir(void);

#endif /* _CVMBOOT_CVMDISK_MOUNT_H */
