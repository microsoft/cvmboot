// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "mount.h"
#include <common/err.h>
#include <utils/err.h>
#include <string.h>
#include <common/buf.h>
#include <sys/stat.h>
#include <common/exec.h>
#include "gpt.h"
#include "eraise.h"

static mount_context_t g_mount_context;

void mount_disk_ex(const char* disk, int flags, bool bind)
{
    mount_context_t* ctx = &g_mount_context;
    path_t target;
    char efi_source[PATH_MAX];

    memset(ctx, 0, sizeof(mount_context_t));

    /* save the mount flags */
    ctx->mount_flags = flags;

    /* find the Linux root partition */
    if (find_gpt_entry_by_type(disk, &linux_type_guid, ctx->source, NULL) < 0)
        ERR("Cannot find Linux root partition: disk=%s", disk);

    /* find the EFI root partition */
    if (find_gpt_entry_by_type(disk, &efi_type_guid, efi_source, NULL) < 0)
        ERR("Cannot find EFI root partition: disk=%s", disk);

    /* Mount the rootfs  partition */
    if (mount(ctx->source, mntdir(), "ext4", flags, NULL) < 0)
    {
        ERR("failed to mount: %s on %s: errno=%d",
            ctx->source, mntdir(), errno);
    }

    /* Push on stack */
    snprintf(target.buf, sizeof(target.buf), "%s", mntdir());
    ctx->mountpoints[ctx->num_mountpoints++] = target;

    /* Mount the EFI partition */
    {
        makepath2(&target, mntdir(), "/boot/efi");

        if (mount(efi_source, target.buf, "vfat", flags, NULL) < 0)
        {
            ERR("failed to mount: %s on %s: errno=%d",
                efi_source, target.buf, errno);
        }

        /* Push on stack */
        ctx->mountpoints[ctx->num_mountpoints++] = target;
    }

    if (bind)
    {
        /* Bind mount the /dev */
        {
            makepath2(&target, mntdir(), "/dev");

            if (mount("/dev", target.buf, "sysfs", MS_BIND, NULL) < 0)
                ERR("bind mount failed: %s", target.buf);

            ctx->mountpoints[ctx->num_mountpoints++] = target;
        }

        /* Bind mount the /proc */
        {
            makepath2(&target, mntdir(), "/proc");

            if (mount("/proc", target.buf, "procfs", MS_BIND, NULL) < 0)
                ERR("bind mount failed: %s", target.buf);

            ctx->mountpoints[ctx->num_mountpoints++] = target;
        }

        /* Bind mount the /sys */
        {
            makepath2(&target, mntdir(), "/sys");

            if (mount("/sys", target.buf, "devtmpfs", MS_BIND, NULL) < 0)
                ERR("bind mount failed: %s", target.buf);

            ctx->mountpoints[ctx->num_mountpoints++] = target;
        }
    }
}

void mount_disk(const char* disk, int flags)
{
    mount_disk_ex(disk, flags, true);
}

void umount_disk(void)
{
    mount_context_t* ctx = &g_mount_context;
    buf_t buf = BUF_INITIALIZER;

    /* Unmount all directories in reverse order */
    for (size_t i = ctx->num_mountpoints; i > 0; i--)
    {
        path_t target = ctx->mountpoints[i-1];

        if (umount(target.buf) < 0)
            ERR("failed to unmount: %s", target.buf);
    }

#if 1
    /* Run fsck on the EXT4 partition */
    if (!(ctx->mount_flags & MS_RDONLY) && *ctx->source)
        execf(&buf, "e2fsck -f -y %s 2> /dev/null", ctx->source);
#endif

    /* Clear context to prevent umounting in atexit */
    memset(ctx, 0, sizeof(mount_context_t));

    buf_release(&buf);
}

const char* mntdir()
{
    static char _mntdir[PATH_MAX];

    if (*_mntdir == '\0')
    {
        /* This approach leaves behind looop devices */
        char template[] = "/tmp/cvmdisk_XXXXXX";
        struct stat statbuf;

        if (!mkdtemp(template))
            ERR("failed to create temporary directory");

        snprintf(_mntdir, sizeof(_mntdir), "%s", template);

        if (stat(_mntdir, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
            ERR("expected existence of temporary directory: %s", _mntdir);
    }

    return _mntdir;
}
