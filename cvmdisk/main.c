// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <utils/sig.h>
#include <utils/cpio.h>
#include <utils/events.h>
#include <utils/sha256.h>
#include <utils/strings.h>
#include <common/strings.h>
#include <utils/paths.h>
#include <common/err.h>
#include <common/file.h>
#include <common/exec.h>
#include <common/getoption.h>
#include <common/sudo.h>
#include <common/strarr.h>
#include <common/sparsecmp.h>
#include <common/cvmvhd.h>
#include <time.h>
#include "guid.h"
#include "find.h"
#include "gpt.h"
#include "options.h"
#include "verity.h"
#include "loop.h"
#include "colors.h"
#include "eraise.h"
#include "sig.h"
#include "which.h"
#include "events.h"
#include "timestamp.h"
#include "version.h"
#include "sha256.h"
#include "path.h"
#include "mount.h"
#include "loop.h"
#include "sharedir.h"
#include "round.h"
#include "globals.h"
#include "frags.h"
#include "progress.h"
#include "sparse.h"

//#define USE_EFI_EPHEMERAL_DISK

#define THIN_BLOCK_SIZE_UNITS ((size_t)512)

/* expressed in 512-byte units */
#define THIN_BLOCK_SIZE ((size_t)1024)

#define THIN_BLOCK_SIZE_IN_BYTES (THIN_BLOCK_SIZE * THIN_BLOCK_SIZE_UNITS)

/* expressed as multiple of THIN_BLOCK_SIZE */
#define THIN_LOW_WATER_MARK ((size_t)1024)

/*
**==============================================================================
**
** local static definitions:
**
**==============================================================================
*/

#define USERNAME_SIZE 33
#define HOSTNAME_SIZE 254

__attribute__((__used__)) static const char _timestamp[] = TIMESTAMP;

typedef struct user_opt
{
    char username[USERNAME_SIZE];
    char password[PATH_MAX];
    char sshkey[PATH_MAX];
}
user_opt_t;

typedef struct hostname_opt
{
    char buf[HOSTNAME_SIZE];
}
hostname_opt_t;

static int _check_program(const char* name)
{
    int ret = 0;
    char path[PATH_MAX];

    if (which(name, path) != 0)
        ERR("cannot find dependent program executable: %s", name);

    if (access(path, X_OK) < 0)
        ERR("dependent program executable is not executable: %s", name);

    return ret;
}

static const char* strip_mntdir(const char* path)
{
    const char* dirname = mntdir();
    size_t len = strlen(dirname);

    if (strncmp(path, dirname, len) == 0)
    {
        const char* p = path + len;

        if (p[0] == '/' && p[1] == '/')
            p++;

        return p;
    }

    return path;
}

static const char* thin_volume_name(void)
{
    static int _initialized = 0;
    static char tmpdir[] = "/tmp/rootfs_thin_XXXXXX";
    const char* p;

    if (!_initialized)
    {
        if (!mkdtemp(tmpdir))
            ERR("failed to create temporary directory: %s", tmpdir);

        _initialized = true;
    }

    if (!(p = strrchr(tmpdir, '/')))
        ERR("failed to find '/' in %s", tmpdir);

    return p + 1;
}

static bool _same_file(const char* filename1, const char* filename2)
{
    struct stat statbuf1;
    struct stat statbuf2;

    if (stat(filename1, &statbuf1) == 0 &&
        stat(filename2, &statbuf2) == 0 &&
        statbuf1.st_ino == statbuf2.st_ino)
    {
        return true;
    }

    return false;
}

static const char* thin_pool_name(void)
{
    static int _initialized = 0;
    static char tmpdir[] = "/tmp/rootfs_thin_pool_XXXXXX";
    const char* p;

    if (!_initialized)
    {
        if (!mkdtemp(tmpdir))
            ERR("failed to create temporary directory: %s", tmpdir);

        _initialized = true;
    }

    if (!(p = strrchr(tmpdir, '/')))
        ERR("failed to find '/' in %s", tmpdir);

    return p + 1;
}

#if 0
static int _zero_fill_device(
    blockdev_t* dev,
    bool print_progress,
    const char* msg)
{
    int ret = 0;
    progress_t progress;
    uint8_t zeros[VERITY_BLOCK_SIZE];
    const size_t num_blocks = dev->file_size / dev->block_size;

    if (dev->block_size != VERITY_BLOCK_SIZE)
        ERAISE(-EINVAL);

    if (print_progress)
        progress_start(&progress, msg);

    memset(zeros, 0, sizeof(zeros));

    for (size_t i = 0; i < num_blocks; i++)
    {
        ECHECK(blockdev_put(dev, i, zeros, 1));
        progress_update(&progress, i, num_blocks);
    }

    progress_end(&progress);

done:
    return ret;
}
#endif

static void _check_vhd(const char* disk)
{
    blockdev_t* bd = NULL;
    ssize_t byte_count;
    buf_t buf = BUF_INITIALIZER;
    size_t num_blocks;
    const size_t block_size = 512;
    uint8_t block[block_size];
    uint8_t vhd_sig[] = { 'c', 'o', 'n', 'e', 'c', 't', 'i', 'x' };

    /* open the disk for read */
    if (blockdev_open(disk, O_RDONLY, 0, block_size, &bd) != 0)
        ERR("VHD not found: %s", disk);

    /* get the size of the disk in bytes */
    if ((byte_count = blockdev_get_size(bd)) < 0)
        ERR("cannot determine VHD size: %s", disk);

    /* fail if the disk is less than one block in size */
    if (byte_count < block_size)
        ERR("VHD is shorter than %zu bytes: %s", block_size, disk);

    /* fail if disk is not a multiple of the block size */
    if ((byte_count % block_size) != 0)
        ERR("VHD is not a multiple of %zu: %s", block_size, disk);

    /* calculate the total number of disk blocks */
    num_blocks = byte_count / block_size;

    /* If unable to read final block of file */
    if (blockdev_get(bd, num_blocks - 1, block, 1) < 0)
        ERR("cannot read last block of VHD: %s", disk);

    /* If the last block is a VHD trailer, then remove from total size */
    if (memcmp(block, vhd_sig, sizeof(vhd_sig)) != 0)
        ERR("Not a VHD file (missing VHD trailer): %s", disk);

    blockdev_close(bd);
    buf_release(&buf);
}

static void _dump_expected_pcr_and_log_contents(
    const char* disk,
    const sig_t* sig)
{
    char events_path[PATH_MAX] = "";
    char source[PATH_MAX];

    if (find_gpt_entry_by_type(disk, &efi_type_guid, source, NULL) < 0)
        ERR("Cannot find EFI partition: %s", disk);

    if (mount(source, mntdir(), "vfat", 0, NULL) < 0)
        ERR("Failed to mount EFI directory: %s => %s", source, mntdir());

    /* If no events command-line argument */
    {
        paths_set_prefix("");
        paths_get(events_path, FILENAME_EVENTS, mntdir());
        paths_set_prefix("/boot/efi");

        if (access(events_path, R_OK) != 0)
            *events_path = '\0';
    }

    /* Use events file; else use signer*/
    if (*events_path)
    {
        sha256_string_t signer;
        process_events_callback_data_t cbd;

        sha256_format(&signer, (const sha256_t*)sig->signer);
        memset(&cbd, 0, sizeof(cbd));

        if (process_events(events_path, signer.buf, &cbd) < 0)
            ERR("failed to process events file: %s", events_path);

        for (size_t i = 0; i < cbd.num_events; i++)
        {
            int pcrnum = cbd.events[i].pcrnum;
            sha256_t digest = cbd.events[i].digest;
            sha256_string_t str;
            sha256_format(&str, &digest);

            printf("%sLOG[%d:%s]%s\n",
                colors_cyan, pcrnum, str.buf, colors_reset);
        }

        /* Print out all the non-zero SHA-256 PCRs */
        for (size_t i = 0; i < MAX_PCRS; i++)
        {
            const sha256_t zeros = SHA256_INITIALIZER;
            const sha256_t pcr = cbd.sha256_pcrs[i];

            if (memcmp(&pcr, &zeros, sizeof(pcr)) != 0)
            {
                sha256_string_t str;
                sha256_format(&str, &pcr);
                printf("%sPCR[%zu]=%s%s\n",
                    colors_cyan, i, str.buf, colors_reset);
            }
        }
    }
    else
    {
        sha256_t pcr11 = SHA256_INITIALIZER;
        sha256_t hash;
        sha256_string_t str;

        sha256_compute(&hash, sig->signer, sizeof(sig->signer));
        sha256_format(&str, &hash);
        printf("%sLOG[%d:%s]%s\n", colors_cyan, 11, str.buf, colors_reset);

        sha256_extend(&pcr11, &hash);
        sha256_format(&str, &pcr11);
        printf("%sPCR[11]=%s%s\n", colors_cyan, str.buf, colors_reset);
    }

    if (umount(mntdir()) < 0)
        ERR("failed to unmount: %s", mntdir());
}

static void _patch_fstab(const char* disk)
{
    buf_t buf = BUF_INITIALIZER;
    path_t fstabfile;
    path_t sedfile;

    printf("%s>>> Patching fstab...%s\n", colors_green, colors_reset);

    mount_disk(disk, 0);

    makepath2(&sedfile, sharedir(), "fstab.sed");
    if (access(sedfile.buf, R_OK) < 0)
        ERR("Cannot locate file: %s", sedfile.buf);

    makepath2(&fstabfile, mntdir(), "etc/fstab");
    if (access(fstabfile.buf, F_OK) < 0)
        ERR("fstab file not found: %s", fstabfile.buf);
    if (access(fstabfile.buf, W_OK) < 0)
        ERR("fstab file is not writable: %s", fstabfile.buf);

    printf("Updating %s:%s...\n", globals.disk, strip_mntdir(fstabfile.buf));
    execf(&buf, "sed -i -f %s %s", sedfile.buf, fstabfile.buf);

    umount_disk();

    buf_release(&buf);
}

// Prevent the ephemeral resource disk (/dev/sdb) from being reformatted.
void _preserve_resource_disk(const char* disk)
{
    buf_t buf = BUF_INITIALIZER;

    printf("%s>>> Preserving resource disk...%s\n",
        colors_green, colors_reset);

    mount_disk(disk, 0);

#if 0
    // Adding new config file to /etc/cloud/cloud.cfg.d to prevent
    // mounting of /dev/sdb1 over /mnt
    {
        const char filename[] = "80-cvmboot.cfg";
        path_t src;
        path_t dest;

        makepath2(&src, sharedir(), filename);
        makepath3(&dest, mntdir(), "etc/cloud/cloud.cfg.d", filename);

        printf("Creating %s...\n", dest.buf);
        execf(&buf, "cp %s %s", src.buf, dest.buf);
    }
#endif

    // Update /etc/cloud/cloud.cfg
    {
        const char filename[] = "cloud.cfg";
        path_t src;
        path_t dest;

        makepath2(&src, sharedir(), filename);
        makepath2(&dest, mntdir(), "etc/cloud/cloud.cfg");

        // Remove lines starting with the one matching "__cvmdisk__"
        execf(&buf, "sed -i '/__cvmdisk__/Q' %s", dest.buf);

        // Append file to /etc/cloud/cloud.cfg
        execf(&buf, "cat %s >> %s", src.buf, dest.buf);
    }

    umount_disk();
    buf_release(&buf);
}

/* ATTN: is it safe to replace resolv.conf? */
static void _update_resolv_conf(const char* disk)
{
    path_t src;
    path_t dest;
    buf_t buf = BUF_INITIALIZER;

    /* mount the root system (plus /boot/efi, /sys, /dev, /proc) */
    mount_disk(disk, 0);

    /* Remove resolv.conf */
    execf(&buf, "rm -f %s/etc/resolv.conf", mntdir());

    makepath2(&src, sharedir(), "/resolv.conf");
    makepath2(&dest, mntdir(), "/etc/resolv.conf");

    if (access(src.buf, R_OK) < 0)
        printf("cannot read file: %s", src.buf);

    execf(&buf, "cp %s %s", src.buf, dest.buf);

    if (access(dest.buf, F_OK) < 0)
        printf("failed to create destination file: %s", dest.buf);

    /* Change file permissions to: -rwxr-xr-x */
    if (chmod(dest.buf, 0755) < 0)
        ERR("failed to change mode: %s", dest.buf);

    umount_disk();

    buf_release(&buf);
}

/* Get names of files in the current directory with given optional prefix */
static int _glob_directory(
    const char* dirname,
    const char* prefix,
    strarr_t* strarr)
{
    int ret = 0;
    DIR* dir = NULL;
    struct dirent* ent;
    size_t prefix_len = strlen(prefix);

    if (!dirname || !prefix || !strarr)
        ERAISE(-EINVAL);

    if (!(dir = opendir(dirname)))
        ERAISE(-ENOENT);

    while ((ent = readdir(dir)))
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        if (!prefix || strncmp(ent->d_name, prefix, prefix_len) == 0)
        {
            if (strarr_append(strarr, ent->d_name) < 0)
                ERAISE(-ENOMEM);
        }
    }

    strarr_sort(strarr);

done:

    if (dir)
        closedir(dir);

    return ret;
}

/* Search given directory for highest-versioned UKI/kernel */
static int _find_uki_or_kernel(
    const char* dirname,
    const char* prefix,
    char path[PATH_MAX],
    char version[PATH_MAX])
{
    int ret = 0;
    const size_t prefix_len = strlen(prefix);
    strarr_t filenames = STRARR_INITIALIZER;

    *path = '\0';
    *version = '\0';

    ECHECK(_glob_directory(dirname, prefix, &filenames));

    if (filenames.size == 0)
        ERAISE(-ENOENT);

    for (size_t i = 0; i < filenames.size; i++)
        printf("Found: %s\n", filenames.data[i]);

    /* select last element of filenames[] */
    const char* str = filenames.data[filenames.size-1];
    strlcpy3(path, dirname, "/", str, PATH_MAX);
    strlcpy(version, str + prefix_len, PATH_MAX);

    strarr_release(&filenames);

done:

    return ret;
}

/* Search for highest-versioned UKI in the /boot/efi/EFI/ubuntu directory */
static int _find_uki(char path[PATH_MAX], char version[PATH_MAX])
{
    int ret = 0;
    char dirname[PATH_MAX];

    *path = '\0';
    *version = '\0';
    strlcpy2(dirname, mntdir(), "/boot/efi/EFI/ubuntu", PATH_MAX);
    ECHECK(_find_uki_or_kernel(dirname, "kernel.efi-", path, version));

done:
    return ret;
}

/* Search for highest-versioned kernel in the /boot directory */
static int _find_kernel(char path[PATH_MAX], char version[PATH_MAX])
{
    int ret = 0;
    char dirname[PATH_MAX];

    *path = '\0';
    *version = '\0';
    strlcpy2(dirname, mntdir(), "/boot", PATH_MAX);
    ECHECK(_find_uki_or_kernel(dirname, "vmlinuz-", path, version));

done:
    return ret;
}

static void _remove_cvmboot_conf(const char* disk)
{
    buf_t buf = BUF_INITIALIZER;
    char path[PATH_MAX];

    paths_get(path, FILENAME_CVMBOOT_CONF, mntdir());
    mount_disk(disk, 0);
    execf(&buf, "rm -f %s", path);
    umount_disk();
    buf_release(&buf);
}

static void _remove_cvmboot_dir(const char* disk)
{
    buf_t buf = BUF_INITIALIZER;
    char path[PATH_MAX];

    paths_get(path, DIRNAME_CVMBOOT_HOME, mntdir());
    mount_disk(disk, 0);
    execf(&buf, "rm -rf %s*", path);
    umount_disk();
    buf_release(&buf);
}

/* Install the kernel onto the EFI system partition */
static void _install_kernel_onto_esp(const char* disk, char version[PATH_MAX])
{
    buf_t buf = BUF_INITIALIZER;
    char path[PATH_MAX];
    char conf_path[PATH_MAX];
    char home[PATH_MAX];
    char dest[PATH_MAX];

    printf("%s>>> Installing kernel onto EFI partition...%s\n",
        colors_green, colors_reset);

    mount_disk(disk, 0);

    /* Create cvmboot home directory */
    paths_get(home, DIRNAME_CVMBOOT_HOME, mntdir());
    execf(&buf, "mkdir -p %s", home);

    /* Find UKI under EFI/ubuntu directory */
    if (_find_uki(path, version) == 0)
    {
        paths_get(dest, DIRNAME_CVMBOOT_HOME, mntdir());
        strlcat(dest, "/vmlinuz-", sizeof(dest));
        strlcat(dest, version, sizeof(dest));

        /* Write kernel to the ESP */
        {
            char dest2[PATH_MAX];

            if (access(path, R_OK) != 0)
                ERR("unable to read file: %s", path);

            printf("Found UKI: %s\n", path);

            /* Extract the kernel from the UKI onto ESP */
            execf(&buf, "objcopy --dump-section .linux=%s %s", dest, path);

            // Also copy kernel to /boot directory (needed by
            // cmdline.boot_image)
            strlcpy2(dest2, mntdir(), "/boot/vmlinuz-", sizeof(dest2));
            strlcat(dest2, version, sizeof(dest2));
            execf(&buf, "rm -f %s", dest2);
            execf(&buf, "cp %s %s", dest, dest2);

            if (access(dest, F_OK) != 0)
                ERR("unable to stat file: %s", dest);

            if (access(dest2, F_OK) != 0)
                ERR("unable to stat file: %s", dest2);

            printf("Created %s:%s\n", globals.disk, strip_mntdir(dest));
        }

#if 0
        /* Extract the Linux command-line from the UKI */
        {
            char cmdline[PATH_MAX];

            paths_get(cmdline, DIRNAME_CVMBOOT_HOME, mntdir());
            strlcat(cmdline, "/cmdline", sizeof(cmdline));

            if (access(path, R_OK) != 0)
                ERR("unable to read file: %s", path);

            execf(&buf, "objcopy --dump-section .cmdline=%s %s", cmdline, path);

            printf("Created %s\n", cmdline);
        }
#endif

        /* Add the 'kernel' option to cvmboot.conf */
        {
            paths_get(conf_path, FILENAME_CVMBOOT_CONF, mntdir());

            if (access(conf_path, F_OK) == 0)
                execf(&buf, "sed -i '/^kernel=/d' %s", conf_path);

            execf(&buf, "echo 'kernel=vmlinuz-%s' >> %s", version, conf_path);
        }

        goto done;
    }

    /* Find kernel with the latest version number */
    if (_find_kernel(path, version) == 0)
    {
        paths_get(dest, DIRNAME_CVMBOOT_HOME, mntdir());
        strlcat(dest, "/vmlinuz-", sizeof(dest));
        strlcat(dest, version, sizeof(dest));

        /* write kernel to ESP */
        {
            if (access(path, R_OK) != 0)
                ERR("unable to read file: %s", path);

            printf("Using kernel: %s:%s\n", globals.disk, strip_mntdir(path));

            /* copy the kernel to the ESP */
            execf(&buf, "cp %s %s", path, dest);

            if (access(dest, F_OK) != 0)
                ERR("unable to stat file: %s", dest);

            printf("Created %s:%s\n", globals.disk, strip_mntdir(dest));
        }

        /* Add the 'kernel' option to cvmboot.conf */
        {
            paths_get(conf_path, FILENAME_CVMBOOT_CONF, mntdir());

            if (access(conf_path, F_OK) == 0)
                execf(&buf, "sed -i '/^kernel=/d' %s", conf_path);

            execf(&buf, "echo 'kernel=vmlinuz-%s' >> %s", version, conf_path);
        }

        goto done;
    }

    ERR("failed to find a suitable kernel");

done:

    umount_disk();

    buf_release(&buf);
}

static void _install_sharedir_file(
    const char* src_suffix,
    const char* dest_suffix)
{
    path_t src;
    path_t dest;
    buf_t buf = BUF_INITIALIZER;

    makepath2(&src, sharedir(), src_suffix);
    makepath2(&dest, mntdir(), dest_suffix);
    execf(&buf, "cp %s %s", src.buf, dest.buf);

    if (chmod(dest.buf, 0755) < 0)
        ERR("failed to change mode: %s", dest.buf);

    printf("Created %s:%s\n", globals.disk, strip_mntdir(dest.buf));
}

static void _cleanup_sharedir_file(const char* suffix)
{
    path_t path;

    makepath2(&path, mntdir(), suffix);
    unlink(path.buf);
    printf("Removed %s:%s\n", globals.disk, strip_mntdir(path.buf));
}

static size_t _get_num_sectors(const char* dev)
{
    size_t n;
    char* end;
    buf_t buf = BUF_INITIALIZER;

    execf(&buf, "blockdev --getsz %s", dev);

    if ((n = strtoull(buf_str(&buf), &end, 10)) < 0 || !end || *end != '\0')
        ERR("blockdev failed on %s", dev);

    buf_release(&buf);

    return (size_t)n;
}

/* Install the Linux initrd onto the EFI system partition */
static void _install_initrd_onto_esp(
    const char* disk,
    const char* version,
    bool use_resource_disk,
    bool use_thin_provisioning)
{
    buf_t buf = BUF_INITIALIZER;

    printf("%s>>> Updating initrd...%s\n", colors_green, colors_reset);

    mount_disk(disk, 0);

    printf("Installing files on target disk...\n");

    /* cleanup any hook files leftover from previous run */
    _cleanup_sharedir_file("/etc/initramfs-tools/hooks/cvmboot");
    _cleanup_sharedir_file("/etc/initramfs-tools/hooks/cvmboot-resource-disk");
    _cleanup_sharedir_file("/etc/initramfs-tools/hooks/cvmboot-thin");

    /* install cvmboot.hook */
    _install_sharedir_file(
        "/cvmboot.hook",
        "/etc/initramfs-tools/hooks/cvmboot");

    /* install cvmboot-resource-disk.hook */
    if (use_resource_disk)
    {
        _install_sharedir_file(
            "/cvmboot-resource-disk.hook",
            "/etc/initramfs-tools/hooks/cvmboot-resource-disk");
    }

    /* install cvmboot-thin-sectors.hook */
    if (use_thin_provisioning)
    {
        path_t src;
        path_t dest;
        char* format;
        size_t format_size;
        char* content;
        char root_dev[PATH_MAX];
        size_t num_thin_sectors;

        if (find_gpt_entry_by_type(disk, &linux_type_guid, root_dev, NULL) < 0)
            ERR("Cannot find Linux partition: disk=%s", disk);

        num_thin_sectors = _get_num_sectors(root_dev);

        makepath2(&src, sharedir(), "/cvmboot-thin.hook");
        makepath2(&dest, mntdir(), "/etc/initramfs-tools/hooks/cvmboot-thin");

        if (load_file(src.buf, (void**)&format, &format_size) != 0)
            ERR("failed to load file: %s", src.buf);

        if (asprintf(&content, format, num_thin_sectors) < 0)
            ERR("out of memory");

        if (write_file(dest.buf, content, strlen(content)) < 0)
            ERR("failed to write file: %s", dest.buf);

        if (chmod(dest.buf, 0755) < 0)
            ERR("chmod failed: %s", dest.buf);

        free(format);
        free(content);
    }

    /* install cvmboot_premount.script */
    {
        _install_sharedir_file(
            "/cvmboot_premount.script",
            "/etc/initramfs-tools/scripts/local-premount/cvmboot_premount");
    }

    /* install cvmboot_bottom.script */
    {
        char path[PATH_MAX] = "/cvmboot_bottom.script";

        if (use_resource_disk)
            strlcat(path, ".resource-disk", sizeof(path));

        _install_sharedir_file(
            path,
            "/etc/initramfs-tools/scripts/init-bottom/cvmboot_bottom");
    }

#ifdef USE_EFI_EPHEMERAL_DISK
    /* install cvmboot_efi.script */
    {
        char path[PATH_MAX] = "/cvmboot_efi.script";

        _install_sharedir_file(
            path,
            "/etc/initramfs-tools/scripts/init-bottom/cvmboot_efi");
    }
#endif /* USE_EFI_EPHEMERAL_DISK */

    /* Generate initrd.img on the EFI partition */
    {
        char path[PATH_MAX];
        char fullpath[PATH_MAX];

        printf("Generating initrd.img for kernel version %s...\n", version);

        paths_get(path, DIRNAME_CVMBOOT_HOME, NULL);
        strlcat(path, "/initrd.img-", sizeof(path));
        strlcat(path, version, sizeof(path));

        execf(&buf, "chroot %s mkinitramfs -o %s %s", mntdir(), path, version);

        strlcpy3(fullpath, mntdir(), "/", path, sizeof(fullpath));

        if (access(fullpath, F_OK) < 0)
            printf("failed to create file: %s\n", fullpath);

        printf("Created %s:%s\n", globals.disk, strip_mntdir(fullpath));
    }

    /* Add the 'initrd' option to cvmboot.conf */
    {
        char conf_path[PATH_MAX];

        paths_get(conf_path, FILENAME_CVMBOOT_CONF, mntdir());

        if (access(conf_path, F_OK) == 0)
            execf(&buf, "sed -i '/^initrd=/d' %s", conf_path);

        execf(&buf, "echo 'initrd=initrd.img-%s' >> %s", version, conf_path);
    }

    umount_disk();

    buf_release(&buf);
}

static void _remove_kvp_service(const char* disk)
{
    path_t path;
    buf_t buf = BUF_INITIALIZER;

    printf("%s>>> Removing the KVP service...%s\n", colors_green, colors_reset);

    mount_disk(disk, 0);

    makepath2(&path, mntdir(), "etc/cloud/cloud.cfg.d/10-azure-kvp.cfg");

    printf("Removing %s:%s...\n", globals.disk, strip_mntdir(path.buf));
    execf(&buf, "rm -f %s", path.buf);

    printf("Disabling KVP service...\n");
    execf(&buf, "chroot %s /usr/bin/systemctl disable hv-kvp-daemon.service",
        mntdir());

    umount_disk();

    buf_release(&buf);
}

static void _add_user(const char* disk, const user_opt_t* user)
{
    buf_t buf = BUF_INITIALIZER;
    path_t homedir;
    struct stat statbuf;
    char* pw = NULL;

    printf("%s>>> Adding user: %s...%s\n",
        colors_green, user->username, colors_reset);

    mount_disk(disk, 0);

    // Format user's home directory
    makepath3(&homedir, mntdir(), "/home", user->username);

    /* if password filename is non-empty, then load the password */
    if (user->password && *user->password)
    {
        size_t size = 0;

        if (load_file(user->password, (void**)&pw, &size) != 0)
            ERR("failed to load password file: %s", user->password);

        // Trim leading and trailing spaces:
        strltrim(pw);
        strrtrim(pw);
    }

    // Only create user if user's home directory does not exist
    if (stat(homedir.buf, &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
    {
        printf("Skipping user creation: home directory already exists: %s\n",
            user->username);
    }
    else
    {
        printf("Creating user: %s\n", user->username);

        if (pw)
        {
            execf(&buf,
                "chroot %s useradd %s -p '%s'", mntdir(), user->username, pw);
        }
        else
        {
            execf(&buf, "chroot %s useradd %s", mntdir(), user->username);
        }

        execf(&buf, "chroot %s mkdir -p /home/%s", mntdir(), user->username);
        execf(&buf, "chroot %s chown -R %s.%s /home/%s",
            mntdir(), user->username, user->username, user->username);
        // execf(&buf, "chroot %s usermod -aG sudo %s", mntdir(), username);
        execf(&buf, "chroot %s adduser %s sudo", mntdir(), user->username);
    }

    /* add ssh-key to authorized keys */
    if (*user->sshkey)
    {
        path_t authkeys;

        makepath4(&authkeys, mntdir(), "/home", user->username,
            "/.ssh/authorized_keys");

        execf(&buf, "mkdir -p %s/home/%s/.ssh", mntdir(), user->username);

        execf(&buf, "chroot %s chown -R %s.%s /home/%s/.ssh",
            mntdir(), user->username, user->username, user->username);

        /* if authorized_keys already exists */
        if (access(authkeys.buf, F_OK) == 0)
        {
            char* src;
            size_t src_size;
            char* dest;
            size_t dest_size;

            /* load ssh-key file into memory */
            if (load_file(user->sshkey, (void**)&src, &src_size) != 0)
                ERR("failed to load file: %s", user->sshkey);

            /* verity that ssh-key-file is in fact a key */
            if (memcmp(src, "ssh-rsa", 7) != 0)
                ERR("not an ssh-key-file: %s", user->sshkey);

            /* load authorized_keys file into memory */
            if (load_file(authkeys.buf, (void**)&dest, &dest_size) != 0)
                ERR("failed to load file: %s", authkeys.buf);

            strrtrim(src);
            strrtrim(dest);

            /* If authorized_keys does not already contain key */
            if (!strstr(dest, src))
                execf(&buf, "cat %s >> %s", user->sshkey, authkeys.buf);

            free(src);
            free(dest);
        }
        else
        {
            execf(&buf, "cp %s %s", user->sshkey, authkeys.buf);
            execf(&buf, "chmod 600 %s", authkeys.buf);
            execf(&buf,
                "chroot %s chown -R %s.%s /home/%s/.ssh/authorized_keys",
                mntdir(), user->username, user->username, user->username);
        }
    }

    umount_disk();

    buf_release(&buf);
}

static void _set_hostname(const char* disk, const char* hostname)
{
    buf_t buf = BUF_INITIALIZER;
    char line1[PATH_MAX];
    char line2[PATH_MAX];

    snprintf(line1, sizeof(line1), "127.0.0.1 localhost");
    snprintf(line2, sizeof(line2), "127.0.1.1 %s", hostname);

    printf("%s>>> Setting the hostname...%s\n", colors_green, colors_reset);

    mount_disk(disk, 0);

    // Update /etc/hostname:
    {
        printf("Updating %s:/etc/hostname...\n", globals.disk);
        execf(&buf, "echo %s > %s/etc/hostname", hostname, mntdir());
    }

    // Update /etc/hosts:
    {
        printf("Updating %s:/etc/hosts...\n", globals.disk);

        // If old 127.0.1.1 line exists, replace with line2:
        execf(&buf,
            "sed -i 's/127.0.1.1.*/%s/g' %s/etc/hosts", line2, mntdir());

        // Append 127.0.1.1 <hostname> to file if not already there:
        if (execf_return(
            &buf, "grep -q '%s' %s/etc/hosts", line2, mntdir()) != 0)
        {
            execf(&buf,
                "sed -i 's/%s/&\\n%s/g' %s/etc/hosts", line1, line2, mntdir());
        }
    }

    umount_disk();
    buf_release(&buf);
}

static int _install_bootloader(const char* disk, const char* events)
{
    int ret = 0;
    buf_t buf = BUF_INITIALIZER;

    printf("%s>>> Installing boot loader...%s\n", colors_green, colors_reset);

    /* Attempt to mount the file system */
    mount_disk(disk, 0);

    /* copy boot loader onto EFI partition */
    {
        char src[PATH_MAX + 32];
        char dest[PATH_MAX + 32];

        snprintf(src, sizeof(src), "%s/cvmboot.efi", sharedir());
        snprintf(dest, sizeof(dest),
            "%s/boot/efi/EFI/BOOT/BOOTX64.EFI", mntdir());

        if (access(src, R_OK) != 0)
            ERR("Unable to locate bootloader: %s", src);

        printf("Creating %s:%s...\n", globals.disk, strip_mntdir(dest));
        execf(&buf, "cp %s %s", src, dest);

        if (events)
        {
            paths_get(dest, FILENAME_EVENTS, mntdir());
            printf("Creating %s:%s...\n", globals.disk, strip_mntdir(dest));
            execf(&buf, "cp %s %s", events, dest);
        }
        else
        {
            paths_get(dest, FILENAME_EVENTS, mntdir());
            printf("Removing %s...\n", dest);
            execf(&buf, "rm -f %s", dest);
        }
    }

    /* create a timestamp file on the ESP */
    {
        char prefix[] = "__timestamp__: ";
        const char* timestamp = TIMESTAMP;
        char conf_path[PATH_MAX];

        if (strncmp(timestamp, prefix, sizeof(prefix)-1) != 0)
            ERR("malformed timestamp: %s", timestamp);

        timestamp += sizeof(prefix)-1;
        paths_get(conf_path, FILENAME_CVMBOOT_CONF, mntdir());

        if (access(conf_path, F_OK) == 0)
            execf(&buf, "sed -i '/^timestamp=/d' %s", conf_path);
        execf(&buf, "echo 'timestamp=%s' >> %s", timestamp, conf_path);
    }

    /* Unmount the file system */
    umount_disk();

    buf_release(&buf);

    return ret;
}

/* ATTN: should this be a function in guid.h? */
/* UUID="594716fc-4c3f-47c8-b9ef-e2b2a34fb137" */
static int _parse_uuid_string(char* str, char uuid[GUID_STRING_SIZE])
{
    int ret = -1;
    char* p = str;
    char* end = str + strlen(str);
    size_t len = end - p;

    if (len < GUID_STRING_LENGTH + 7)
        goto done;

    if (strncmp(p, "UUID=", 5) != 0)
        goto done;

    p += 5;

    if (*p != '"')
        goto done;

    p++;

    if (p[GUID_STRING_LENGTH] != '"')
        goto done;

    p[GUID_STRING_LENGTH] = '\0';

    if (guid_valid_str(p) != 0)
        goto done;

    memcpy(uuid, p, GUID_STRING_LENGTH);
    uuid[GUID_STRING_LENGTH] = '\0';

    ret = 0;

done:
    return ret;
}

static void _append_cmdline_option(const char* disk, const char* version)
{
    buf_t buf = BUF_INITIALIZER;
    buf_t boot_image = BUF_INITIALIZER;
    char uuid[GUID_STRING_SIZE];
    char conf_path[PATH_MAX];
    char* linux_cmdline = NULL;
    char loop[PATH_MAX];

    printf("%s>>> Appending 'cmdline' option to cvmboot.conf...%s\n",
        colors_green, colors_reset);

    /* Get path of Linux EXT4 partition */
    if (find_gpt_entry_by_type(disk, &linux_type_guid, loop, NULL) < 0)
        ERR("Cannot find Linux root partition: disk=%s", disk);

    mount_disk(disk, 0);

    /* Remove cmdline file from the EFI partition */
    paths_get(conf_path, FILENAME_CVMBOOT_CONF, mntdir());

    /* Get the path of the boot image */
    execf(&boot_image, "chroot %s realpath /boot/vmlinuz", mntdir());

    // Get the UUID for the rootfs:
    {
        char* clone;
        char* p;

        execf(&buf, "blkid %s", loop);

        if (!(clone = strdup(buf_str(&buf))))
            ERR("out of memory");

        if (!(p = strstr(clone, "UUID=")))
            ERR("cannot find 'UUID=' token");

        if (_parse_uuid_string(p, uuid) != 0)
            ERR("UUID string is malformed: %s", buf_str(&buf));

        free(clone);
    }

    /* Format the linux_cmdline */
    if (strstr(version, "-azure"))
    {
        /* Add special console parameters for azure images */
        if (asprintf(
            &linux_cmdline,
            "BOOT_IMAGE=%s root=UUID=%s ro console=tty1 console=ttyS0\n",
            buf_str(&boot_image),
            uuid) < 0)
        {
            ERR("failed to format the linux_cmdline");
        }
    }
    else
    {
        /* Add normal parameters for azure images */
        if (asprintf(
            &linux_cmdline,
            "BOOT_IMAGE=%s root=UUID=%s ro",
            buf_str(&boot_image),
            uuid) < 0)
        {
            ERR("failed to format the linux_cmdline");
        }
    }

    strrtrim(linux_cmdline);
    printf("linux_cmdline=%s\n", linux_cmdline);

    /* Append 'cmdline' to the configuration file */
    {
        if (access(conf_path, F_OK) == 0)
            execf(&buf, "sed -i '/^cmdline=/d' %s", conf_path);

        execf(&buf, "echo 'cmdline=%s' >> %s", linux_cmdline, conf_path);
    }

    printf("Added cmdline option to %s:%s\n",
        globals.disk, strip_mntdir(conf_path));

    umount_disk();

    buf_release(&buf);
    buf_release(&boot_image);
    free(linux_cmdline);
}

static void _atexit_function(void)
{
    buf_t buf = BUF_INITIALIZER;
    char dirname[PATH_MAX];

    umount_disk();

    if (*globals.loop)
        lodetach(globals.loop);

    if (*mntdir())
        rmdir(mntdir());

    strlcpy2(dirname, "/tmp/", thin_volume_name(), sizeof(dirname));
    rmdir(dirname);

    strlcpy2(dirname, "/tmp/", thin_pool_name(), sizeof(dirname));
    rmdir(dirname);

    buf_release(&buf);
}

static void _setup_loopback(int argc, const char* argv[])
{
    if (argc >= 3)
    {
        globals.disk = argv[2];
        losetup(globals.disk, globals.loop);
        argv[2] = globals.loop;
        atexit(_atexit_function);
    }
}

static void _check_root()
{
    // Check for root privileges.
    if (geteuid() != 0)
        ERR("%srequires root privileges%s", colors_red, colors_reset);
}

static size_t _get_ext4_block_size(const char* partition)
{
    buf_t buf = BUF_INITIALIZER;
    const char* p;
    char *end;
    size_t n;

    execf(&buf,
        "dumpe2fs %s 2> /dev/null | grep \"Block size:\"", partition);

    p = buf_str(&buf) + strcspn(buf_str(&buf), "0123456789");

    if ((n = strtoull(p, &end, 10)) < 0 || !end || *end != '\0')
        ERR("failed to get block size from dumpe2fs output");

    if (n != 512 && n != 1024 && n != 4096)
        ERR("unexpected block size: %zu", n);

    buf_release(&buf);

    return n;
}

static size_t _get_ext4_block_count(const char* partition)
{
    buf_t buf = BUF_INITIALIZER;
    const char* p;
    char *end;
    size_t n;

    execf(&buf,
        "dumpe2fs %s 2> /dev/null | grep \"Block count:\"", partition);

    p = buf_str(&buf) + strcspn(buf_str(&buf), "0123456789");

    if ((n = strtoull(p, &end, 10)) < 0 || !end || *end != '\0')
        ERR("failed to get block count from dumpe2fs output");

    if (n == 0)
        ERR("unexpected block count: %zu", n);

    buf_release(&buf);

    return n;
}

static size_t _get_gpt_unused_space(const char* disk)
{
    gpt_t* gpt;
    ssize_t n = 0;

    if (gpt_open(disk, O_RDONLY, &gpt) < 0)
        ERR("failed to open the GUID partition table: %s", disk);

    if ((n = gpt_trailing_free_space(gpt)) < 0)
        ERR("failed to get trailing free space");

    gpt_close(gpt);

    return (size_t)n;
}

/* Test whether partition is an EXT4 root file system */
static int __test_ext4_rootfs(const char* part)
{
    int ret = -1;
    struct stat statbuf;
    char path[PATH_MAX];

    /* Attempt to mount the file system */
    if (mount(part, mntdir(), "ext4", 0, NULL) < 0)
        goto done;

    /* Form path of "/sbin/init" */
    strlcpy2(path, mntdir(), "/sbin/init", sizeof(path));

    /* Check for existence of "/sbin/init" */
    if (stat(path, &statbuf) < 0)
        goto done;

    /* Unmount the file system */
    if (umount(mntdir()) < 0)
        goto done;

    ret = 0;

done:
    return ret;
}

/* expand EXT4 partition to fill the entire disk */
static void _expand_ext4_root_partition(const char* disk)
{
    int part_index;
    char source[2*PATH_MAX];
    const guid_t guid = linux_type_guid;
    ssize_t num_sectors;
    size_t block_size;
    size_t block_count;
    size_t block_count_0;
    size_t block_count_1;
    size_t block_count_2;
    buf_t buf = BUF_INITIALIZER;

    printf("%s>>> Expanding EXT4 Linux root partition...%s\n",
        colors_green, colors_reset);

    /* find the Linux root partition */
    if ((part_index = find_gpt_entry_by_type(disk, &guid, source, NULL)) < 0)
        ERR("Cannot find Linux root partition: disk=%s", disk);

    /* Run fsck on the EXT4 partition */
    execf(&buf, "e2fsck -f -y %s 2> /dev/null", source);

    /* Check whether partition is really a Linux root partition */
    if (__test_ext4_rootfs(source) < 0)
        ERR("partition is not an EXT4 rootfs partition: %s", source);

    /* Expand partition to maximum size */
    {
        gpt_t* gpt;

        printf("Expanding root partition (%s)...\n", source);

        /* Open the GUID partition table */
        if (gpt_open(disk, O_RDWR | O_EXCL, &gpt) < 0)
            ERR("failed to open the GUID partition table: %s", disk);

        gpt_entry_t e;
        if (gpt_get_entry(gpt, part_index, &e) < 0)
            ERR("failed to get GPT entry");
        block_count_0 = ((e.ending_lba - e.starting_lba + 1) * 512) / 4096;

        /* resize the partition */
        if ((num_sectors = gpt_resize_partition(gpt, part_index, 0)) < 0)
            ERR("failed to expand partition: %s: part=%d", disk, part_index);

        if (gpt_get_entry(gpt, part_index, &e) < 0)
            ERR("failed to get GPT entry");
        block_count_1 = ((e.ending_lba - e.starting_lba + 1) * 512) / 4096;
        block_count_2 = (num_sectors * 512) / 4096;

        /* close the GUID partition table */
        gpt_close(gpt);
    }

    assert(block_count_0 <= block_count_1);
    assert(block_count_1 == block_count_2);
    assert(block_count <= block_count_1);

    /* Calculate the block_size and desired EXT4 block_count */
    block_size = _get_ext4_block_size(source);
    block_count = (num_sectors * 512) / block_size;

    /* Run fsck on the EXT4 partition */
    execf(&buf, "e2fsck -f -y %s 2> /dev/null", source);

    /* Resize the EXT4 file system to target size */
    printf("Expanding root file system (%s)...\n", source);
    execf(&buf,
        "resize2fs -f %s %zu 2> /dev/null", source, block_count);

    /* Run fsck on the EXT4 partition */
    execf(&buf, "e2fsck -f -y %s 2> /dev/null", source);

    /* Cross check EXT4 block count against expected block count */
    if (_get_ext4_block_count(source) != block_count)
        ERR("block count is not as expected after EXT4 resize %s", source);

    buf_release(&buf);
}

static void _round_root_partition(const char* disk)
{
    int part_index;
    char source[2*PATH_MAX];
    const guid_t guid = linux_type_guid;
    buf_t buf = BUF_INITIALIZER;

    printf("%s>>> Rounding size of rootfs partition up to 4096 boundary...%s\n",
        colors_green, colors_reset);

    /* find the Linux root partition */
    if ((part_index = find_gpt_entry_by_type(disk, &guid, source, NULL)) < 0)
        ERR("Cannot find rootfs partition: disk=%s", disk);

    /* Enlarge VHD slightly to make room for partition rounding below */
    {
        cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

        /* temporarily detach loopback device */
        lodetach(globals.loop);
        *globals.loop = '\0';

        size_t disk_size = _get_num_sectors(globals.disk) * GPT_SECTOR_SIZE;
        size_t new_size = disk_size + 4096;

        /* resize the VHD device */
        if (cvmvhd_resize(globals.disk, new_size, &err) < 0)
            ERR("%s", err.buf);

        /* restore setup of loopback device */
        /* ATTN: 'disk' variable may be invalided by losetup */
        losetup(globals.disk, globals.loop);
    }

    /* Expand partition to maximum size */
    {
        gpt_t* gpt;
        gpt_entry_t e;
        const size_t sectors_per_block = 4096 / GPT_BLOCK_SIZE;
        ssize_t num_sectors;
        ssize_t desired_num_sectors;

        /* Open the GUID partition table */
        if (gpt_open(disk, O_RDWR | O_EXCL, &gpt) < 0)
            ERR("failed to open the GUID partition table: %s", disk);

        if (gpt_get_entry(gpt, part_index, &e) < 0)
            ERR("failed to get GPT entry");

        num_sectors = (e.ending_lba - e.starting_lba + 1);
        desired_num_sectors = round_up_to_multiple(
            num_sectors, sectors_per_block);

        if (num_sectors != desired_num_sectors)
        {
            printf("Resizing sector from %zu to %zu\n", num_sectors,
                desired_num_sectors);

            if (gpt_resize_partition(gpt, part_index, desired_num_sectors) < 0)
                ERR("cannot expand partition: %s: part=%d", disk, part_index);

            if (gpt_get_entry(gpt, part_index, &e) < 0)
                ERR("failed to get GPT entry");

            if ((e.ending_lba - e.starting_lba + 1) != desired_num_sectors)
                ERR("resize of parition failed");
        }

        /* close the GUID partition table */
        gpt_close(gpt);
    }

    buf_release(&buf);
}

static void _add_partition(
    const char* disk,
    const guid_t* type_guid,
    size_t num_blocks, /* number of blocks of size GPT_BLOCK_SIZE */
    uint64_t attributes,
    const char* type_name)
{
    gpt_t* gpt;
    guid_t unique_guid;
    uint16_t u16_type_name[GPT_ENTRY_TYPENAME_SIZE] = { 0 };

    if (strlen(type_name) > GPT_ENTRY_TYPENAME_SIZE)
        ERR("GPT type_name is too long: %s", type_name);

    /* convert type name to uint16_t */
    {
        const char* src = type_name;
        uint16_t* dest = u16_type_name;

        while (*src)
            *dest++ = *src++;
    }

    if (gpt_open(disk, O_RDWR | O_EXCL, &gpt) < 0)
        ERR("failed to open the GUID partition table: %s", disk);

    guid_generate(&unique_guid);

    if (gpt_add_partition(
        gpt,
        type_guid,
        &unique_guid,
        num_blocks,
        attributes,
        u16_type_name) < 0)
    {
        ERR("failed to add partition: %s: %s", disk, type_name);
    }

    gpt_close(gpt);
}

static void _initialize_thin_partitions(const char* disk)
{
    ssize_t root_index;
    char root_dev[PATH_MAX];
    ssize_t data_index;
    char data_dev[PATH_MAX];
    ssize_t meta_index;
    char meta_dev[PATH_MAX];
    size_t num_data_sectors;
    size_t num_root_sectors;
    size_t num_thin_sectors;
    buf_t buf = BUF_INITIALIZER;
    gpt_entry_t entry;
    char msg[] = "Copying root partition to thin partition";

    printf("%s>>> Initializing thin meta/data partitions...%s\n",
        colors_green, colors_reset);

    execf_return(&buf, "dmsetup remove %s 2> /dev/null", thin_volume_name());
    execf_return(&buf, "dmsetup remove %s 2> /dev/null", thin_pool_name());

    /* find the Linux root partition */
    if ((root_index = find_gpt_entry_by_type(
        disk, &linux_type_guid, root_dev, &entry)) < 0)
    {
        ERR("Cannot find Linux partition: disk=%s", disk);
    }

    /* find the thin data partition */
    if ((data_index = find_gpt_entry_by_type(
        disk, &thin_data_type_guid, data_dev, NULL)) < 0)
    {
        ERR("Cannot find thin data partition: disk=%s", disk);
    }

    /* find the thin meta partition */
    if ((meta_index = find_gpt_entry_by_type(
        disk, &thin_meta_type_guid, meta_dev, NULL)) < 0)
    {
        ERR("Cannot find thin meta partition: disk=%s", disk);
    }

    /* Get the number of data sectors (512 bytes) */
    num_data_sectors = _get_num_sectors(data_dev);

    /* Get the number of root sectors (512 bytes) */
    num_root_sectors = _get_num_sectors(root_dev);

    /* Zero-fill the first 4096 bytes of the meta device */
    printf("Initializing thin meta partition...\n");
    execf(&buf, "dd if=/dev/zero of=%s bs=4096 count=1 status=none", meta_dev);

    /* Create thin pool */
#ifdef VERBOSE_PRINTFS
    printf("Creating /dev/mapper/%s...\n", thin_pool_name());
#endif
    execf(&buf,
        "dmsetup create %s --table \"0 %zu thin-pool %s %s %zu %zu\"",
        thin_pool_name(),
        num_data_sectors,
        meta_dev,
        data_dev,
        THIN_BLOCK_SIZE,
        THIN_LOW_WATER_MARK);

    /* Create the volume */
    printf("Creating thin volume...\n");
    execf(&buf,
        "dmsetup message /dev/mapper/%s 0 \"create_thin 0\"",
        thin_pool_name());

    /* Activate the thin volume */
#ifdef VERBOSE_PRINTFS
    printf("Activating /dev/mapper/%s...\n", thin_volume_name());
#endif
    execf(&buf,
        "dmsetup create %s --table \"0 %zu thin /dev/mapper/%s 0\"",
        thin_volume_name(),
        num_root_sectors,
        thin_pool_name());

    /* Get the number of thin-volume sectors (512 bytes) */
    {
        path_t path;
        makepath2(&path, "/dev/mapper/", thin_volume_name());
        num_thin_sectors = _get_num_sectors(path.buf);
    }

    /* Check that the root partition and thin volume are the same size */
    if (num_root_sectors != num_thin_sectors)
    {
        ERR("root/thin devices are different sizes: %zu/%zu",
            num_root_sectors, num_thin_sectors);
    }

    /* Workaround for busy error produced by "dmsetup remove" below */
    sleep(1);

    /* Copy root partition to thin partition */
    {
        const uint64_t offset = gpt_entry_offset(&entry);
        const uint64_t end = offset + gpt_entry_size(&entry);
        frag_list_t frags = FRAG_LIST_INITIALIZER;
        frag_list_t holes = FRAG_LIST_INITIALIZER;

        if (frags_find(globals.disk, offset, end, &frags, &holes) < 0)
            ERR("frags_find() failed: %s", globals.disk);

        /* Copy data from root partition to thin data partition */
        {
            char thin[PATH_MAX];

            strlcpy2(thin, "/dev/mapper/", thin_volume_name(), sizeof(thin));

            if (frags_copy(&frags, globals.disk, offset, thin, 0, msg) < 0)
                ERR("frags_copy() failed");
        }

        frags_release(&frags);
        frags_release(&holes);
    }

    /* Print the thin-provisioning savings */
    {
        double x = (double)num_root_sectors;
        double y = (double)num_data_sectors;
        double percent = -((y / x - 1.0) * 100.0);
        printf("Saved %4.1lf%% with thin-provisioning\n", percent);
    }

    /* Workaround for busy error produced by "dmsetup remove" below */
    sleep(1);

    /* Remove thin volume */
#ifdef VERBOSE_PRINTFS
    printf("Removing /dev/mapper/%s...\n", thin_volume_name());
#endif
    execf(&buf, "dmsetup remove %s", thin_volume_name());

    /* Remove thin pool */
#ifdef VERBOSE_PRINTFS
    printf("Removing /dev/mapper/%s...\n", thin_pool_name());
#endif
    execf(&buf, "dmsetup remove %s", thin_pool_name());

    buf_release(&buf);
}

static void _verify_thin_partitions(const char* disk)
{
    ssize_t root_index;
    char root_dev[PATH_MAX];
    ssize_t data_index;
    char data_dev[PATH_MAX];
    ssize_t meta_index;
    char meta_dev[PATH_MAX];
    size_t num_root_sectors;
    size_t num_data_sectors;
    size_t num_thin_sectors;
    buf_t buf = BUF_INITIALIZER;
    gpt_entry_t entry;
    char msg[] = "Comparing root partition to thin partition";

    printf("%s>>> Verifying thin meta/data partitions...%s\n",
        colors_green, colors_reset);

    execf_return(&buf, "dmsetup remove %s 2> /dev/null", thin_volume_name());
    execf_return(&buf, "dmsetup remove %s 2> /dev/null", thin_pool_name());

    /* find the Linux root partition */
    if ((root_index = find_gpt_entry_by_type(
        disk, &linux_type_guid, root_dev, &entry)) < 0)
    {
        ERR("Cannot find Linux partition: disk=%s", disk);
    }

    /* find the thin data partition */
    if ((data_index = find_gpt_entry_by_type(
        disk, &thin_data_type_guid, data_dev, NULL)) < 0)
    {
        ERR("Cannot find thin data partition: disk=%s", disk);
    }

    /* find the thin meta partition */
    if ((meta_index = find_gpt_entry_by_type(
        disk, &thin_meta_type_guid, meta_dev, NULL)) < 0)
    {
        ERR("Cannot find thin meta partition: disk=%s", disk);
    }

    /* Get the number of data sectors (512 bytes) */
    num_data_sectors = _get_num_sectors(data_dev);

    /* Get the number of root sectors (512 bytes) */
    num_root_sectors = _get_num_sectors(root_dev);

    /* Activate thin pool */
#ifdef VERBOSE_PRINTFS
    printf("Activating /dev/mapper/%s...\n", thin_pool_name());
#endif
    execf(&buf,
        "dmsetup create %s --table \"0 %zu thin-pool %s %s %zu %zu %s\"",
        thin_pool_name(),
        num_data_sectors,
        meta_dev,
        data_dev,
        THIN_BLOCK_SIZE,
        THIN_LOW_WATER_MARK,
        "1 read_only");

    /* Activate thin volume */
#ifdef VERBOSE_PRINTFS
    printf("Activating /dev/mapper/%s...\n", thin_volume_name());
#endif
    execf(&buf,
        "dmsetup create %s --table \"0 %zu thin /dev/mapper/%s 0\"",
        thin_volume_name(),
        num_root_sectors,
        thin_pool_name());

    /* Get the number of thin-volume sectors (512 bytes) */
    {
        path_t path;
        makepath2(&path, "/dev/mapper/", thin_volume_name());
        num_thin_sectors = _get_num_sectors(path.buf);
    }

    /* Check that the root partition and thin volume are the same size */
    if (num_root_sectors != num_thin_sectors)
    {
        ERR("root/thin devices are different sizes: %zu/%zu",
            num_root_sectors, num_thin_sectors);
    }

    /* Workaround for busy error produced by "dmsetup remove" below */
    sleep(1);

    /* Compare the root-device with the thin-device */
    {
        const uint64_t offset = gpt_entry_offset(&entry);
        const uint64_t end = offset + gpt_entry_size(&entry);
        frag_list_t frags = FRAG_LIST_INITIALIZER;
        frag_list_t holes = FRAG_LIST_INITIALIZER;

        if (frags_find(globals.disk, offset, end, &frags, &holes) < 0)
            ERR("frags_find() failed: %s", globals.disk);

        /* Compare root partition with thin data partition */
        {
            char thin[PATH_MAX];

            strlcpy2(thin, "/dev/mapper/", thin_volume_name(), sizeof(thin));

            if (frags_compare(&frags, offset, globals.disk, thin, msg) < 0)
                ERR("Compare failed");
        }

        frags_release(&holes);
        frags_release(&frags);
    }

    /* Workaround for busy error produced by "dmsetup remove" below */
    sleep(1);

    /* Remove thin volume */
#ifdef VERBOSE_PRINTFS
    printf("Removing /dev/mapper/%s...\n", thin_volume_name());
#endif
    execf(&buf, "dmsetup remove %s", thin_volume_name());

    /* Remove thin pool */
#ifdef VERBOSE_PRINTFS
    printf("Removing /dev/mapper/%s...\n", thin_pool_name());
#endif
    execf(&buf, "dmsetup remove %s", thin_pool_name());

    buf_release(&buf);
}

#define ENLARGE_DISK_MESSAGE \
"%sDisk is too small to accomodate new partition. " \
"Please enlarge disk to %zu gigabytes.%s\n"

/* ATTN: move this function to sparse.c */
static void _punch_hole(const char* path, size_t offset, size_t len)
{
    int fd;
    const int mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;

    if ((fd = open(path, O_RDWR | O_CREAT, 0666)) < 0)
        ERR("failed to open: %s", path);

    if (fallocate(fd, mode, offset, len) < 0)
        ERR("fallocate() failed: error=%d", errno);

    close(fd);
}

static void _add_extra_partitions(
    const char* disk,
    bool use_thin_provisioning,
    bool use_resource_disk,
    bool verify)
{
    int part_index;
    gpt_entry_t entry;
    char source[PATH_MAX];
    buf_t buf = BUF_INITIALIZER;
    size_t ext4_block_size;
    size_t ext4_block_count;
    size_t thin_data_partition_size = 0;
    size_t thin_meta_partition_size = 0;
    size_t available_disk_space;
    size_t extra_space = 0;
    size_t ext4_bytes;
    const size_t gb = 1024*1024*1024;
#ifdef USE_EFI_EPHEMERAL_DISK
    const size_t efi_partition_size = gb;
#endif /* USE_EFI_EPHEMERAL_DISK */

    printf("%s>>> Adding extra partitions...%s\n", colors_green, colors_reset);

    /* find the Linux root partition */
    if ((part_index = find_gpt_entry_by_type(
        disk, &linux_type_guid, source, &entry)) < 0)
    {
        ERR("Cannot find Linux root partition: disk=%s", disk);
    }

    /* Check whether rootfs is valid */
    if (__test_ext4_rootfs(source) < 0)
        ERR("partition is not an EXT4 rootfs partition: %s", source);

    ext4_block_size = _get_ext4_block_size(source);
    ext4_block_count = _get_ext4_block_count(source);
    ext4_bytes = ext4_block_size * ext4_block_count;

    /* Need extra space for upper layer disk */
    if (!use_resource_disk)
        extra_space += ext4_bytes;

    /* Calculate space needed for thin partition */
    if (use_thin_provisioning)
    {
        const size_t two_mb = 2*1024*1024;
        size_t n;
        size_t m;
        const uint64_t offset = gpt_entry_offset(&entry);
        const uint64_t end = offset + gpt_entry_size(&entry);

        /* Find sparse fragments */
        {
            frag_list_t frags = FRAG_LIST_INITIALIZER;
            frag_list_t holes = FRAG_LIST_INITIALIZER;

            if (frags_find(globals.disk, offset, end, &frags, &holes) < 0)
                ERR("frags_find() failed: %s", globals.disk);

            n = frags.num_blocks * ext4_block_size;
            n += gb;

            frags_release(&frags);
            frags_release(&holes);
        }

        n += (THIN_LOW_WATER_MARK * THIN_BLOCK_SIZE_IN_BYTES);
        n = round_up_to_multiple(n, two_mb);

        /* calculate size of thin meta partition (2.5%) */
        m = n / 40;
        m = round_up_to_multiple(m, two_mb);

        thin_data_partition_size = n;
        thin_meta_partition_size = m;
        extra_space += thin_data_partition_size;
        extra_space += thin_meta_partition_size;
    }

    /* Leave room for the verity tree */
    {
        ssize_t n;

        if ((n = verity_hash_dev_size(ext4_bytes)) < 0)
            ERR("verity_hash_dev_size() failed");

        extra_space += n;
    }

#ifdef USE_EFI_EPHEMERAL_DISK
    /* Leave room for the EFI upper-layer partition */
    extra_space += efi_partition_size;
#endif /* USE_EFI_EPHEMERAL_DISK */

    /* Get remaining disk space in bytes */
    available_disk_space = _get_gpt_unused_space(disk);

    /* Print error if disk is not large enough for new partitions */
    if (extra_space > available_disk_space)
    {
        size_t disk_size = _get_num_sectors(disk) * GPT_SECTOR_SIZE;
        size_t needed = extra_space - available_disk_space;
        size_t new_size = round_up_to_multiple(disk_size + needed, gb);
        cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

        /* temporarily detach loopback device */
        lodetach(globals.loop);
        *globals.loop = '\0';

        printf("Expanding %s from %zuGB to %zuGB\n", globals.disk,
            disk_size/gb, new_size/gb);

        /* resize the VHD device */
        if (cvmvhd_resize(globals.disk, new_size, &err) < 0)
            ERR("%s", err.buf);

        /* restore setup of loopback device */
        losetup(globals.disk, globals.loop);
    }

    if (use_thin_provisioning)
    {
        /* Add thin-data partition */
        printf("Adding thin data partition...\n");
        _add_partition(
            disk,
            &thin_data_type_guid,
            thin_data_partition_size / GPT_BLOCK_SIZE,
            0, /* attributes */
            "THIN-DATA");

        /* Zero-fill the thin data partition */
        {
            char path[PATH_MAX];
            gpt_entry_t e;

            printf("Clearing thin data partition...\n");

            if (find_gpt_entry_by_type(
                disk, &thin_data_type_guid, path, &e) < 0)
            {
                ERR("Refusing to strip disk that has no thin data partition");
            }

            // Make whole partition sparse:
            {
                lodetach(globals.loop);
                *globals.loop = '\0';
                const size_t offset = gpt_entry_offset(&e);
                const uint64_t len = gpt_entry_size(&e);
                _punch_hole(globals.disk, offset, len);
                losetup(globals.disk, globals.loop);
            }
        }

        /* Add thin-meta partition */
        printf("Adding thin meta partition...\n");
        _add_partition(
            disk,
            &thin_meta_type_guid,
            thin_meta_partition_size / GPT_BLOCK_SIZE,
            0, /* attributes */
            "THIN-META");

        /* Zero-fill the thin meta partition */
        {
            char path[PATH_MAX];
            gpt_entry_t e;

            printf("Clearing thin meta partition...\n");

            if (find_gpt_entry_by_type(
                disk, &thin_meta_type_guid, path, &e) < 0)
            {
                ERR("Refusing to strip disk that has no thin data partition");
            }

            // Make whole partition sparse:
            {
                lodetach(globals.loop);
                *globals.loop = '\0';
                const size_t offset = gpt_entry_offset(&e);
                const uint64_t len = gpt_entry_size(&e);
                _punch_hole(globals.disk, offset, len);
                losetup(globals.disk, globals.loop);
            }
        }

        /* Initialize the thin meta/data partitions */
        _initialize_thin_partitions(disk);

        /* Test activation of thin partitions and compare ext4/thin */
        if (verify)
            _verify_thin_partitions(disk);
    }

    /* Add rootfs upper layer partition but default to the maximum size */
    if (!use_resource_disk)
    {
        printf("Adding rootfs upper layer partition...\n");

        _add_partition(
            disk,
            &rootfs_upper_type_guid,
            ext4_bytes / GPT_BLOCK_SIZE, /* num_blocks */
            0, /* attributes */
            "ROOTFS-UPPER");
    }

#ifdef USE_EFI_EPHEMERAL_DISK
    /* Add EFI upper layer partition */
    {
        printf("Adding EFI upper layer partition...\n");

        _add_partition(
            disk,
            &efi_upper_type_guid,
            efi_partition_size / GPT_BLOCK_SIZE,
            0, /* attributes */
            "EFI-UPPER");
    }
#endif /* USE_EFI_EPHEMERAL_DISK */

    buf_release(&buf);
}

static void _genkeys(const char* privkey, const char* pubkey)
{
    buf_t buf = BUF_INITIALIZER;

    execf(&buf, "openssl genrsa -out %s", privkey);
    execf(&buf,
        "openssl rsa -in %s -pubout -out %s 2>/dev/null", privkey, pubkey);

    if (access(privkey, R_OK) < 0)
        ERR("Failed to create %s", privkey);

    if (access(pubkey, R_OK) < 0)
        ERR("Failed to create %s", pubkey);

    buf_release(&buf);
}

static int _locate_signtool(const char* signtool, char path[PATH_MAX])
{
    int ret = 0;
    struct stat statbuf;

    if (which(signtool, path) != 0)
        ERAISE(-EINVAL);

    if (stat(path, &statbuf) != 0 || access(path, X_OK) != 0)
        ERAISE(-EINVAL);

done:
    return ret;
}

static void _test_signtool(const char* signtool_path)
{
    int ret = -1;
    char tmpfile[] = "/tmp/cvmboot_XXXXXX";
    char sigfile[PATH_MAX] = "";
    buf_t buf = BUF_INITIALIZER;
    int fd = -1;
    char buffer[512] = { '\0' };

    if ((fd = mkstemp(tmpfile)) < 0)
    {
        ERR_NOEXIT("failed to create temporary file: %s", tmpfile);
        goto done;
    }

    if (write(fd, buffer, sizeof(buffer)) != sizeof(buffer))
    {
        ERR_NOEXIT("failed to write temporary file: %s", tmpfile);
        goto done;
    }

    if (execf_return(&buf, "%s %s", signtool_path, tmpfile) != 0)
    {
        ERR_NOEXIT("failed to verify signing tool: %s", signtool_path);
        goto done;
    }

    strlcpy2(sigfile, tmpfile, ".sig", sizeof(sigfile));

    ret = 0;

done:

    if (fd >= 0)
    {
        close(fd);
        unlink(tmpfile);
    }

    if (*sigfile)
        unlink(sigfile);

    buf_release(&buf);

    if (ret < 0)
        exit(1);
}

static void _purge_disk(
    const char* disk,
    bool purge_thin_partitions,
    bool purge_upper_partitions)
{
    int ret;
    gpt_t* gpt = NULL;
    guid_t type_guid;

    // Open the GUID partition table
    if ((ret = gpt_open(disk, O_RDWR | O_EXCL, &gpt)) < 0)
    {
        ERR("failed to open the GUID partition table: %s: %s",
            disk, strerror(-ret));
    }

    guid_init_str(&type_guid, VERITY_PARTITION_TYPE_GUID);

    if (purge_upper_partitions)
    {
        if ((ret = gpt_remove_partitions(gpt, &rootfs_upper_type_guid,
            g_options.trace)) < 0)
        {
            ERR("failed to remove rootfs upper-layer partition: %s",
                strerror(-ret));
        }

#ifdef USE_EFI_EPHEMERAL_DISK
        if ((ret = gpt_remove_partitions(gpt, &efi_upper_type_guid,
            g_options.trace)) < 0)
        {
            ERR("failed to remove EFI upper-layer partition: %s",
                strerror(-ret));
        }
#endif /* USE_EFI_EPHEMERAL_DISK */
    }

    if (purge_thin_partitions)
    {
        if ((ret = gpt_remove_partitions(gpt, &thin_data_type_guid,
            g_options.trace)) < 0)
        {
            ERR("failed to remove thin data partition: %s", strerror(-ret));
        }

        if ((ret = gpt_remove_partitions(gpt, &thin_meta_type_guid,
            g_options.trace)) < 0)
        {
            ERR("failed to remove thin meta partition: %s", strerror(-ret));
        }
    }

    if ((ret = gpt_remove_partitions(gpt, &type_guid, g_options.trace)) < 0)
    {
        ERR("failed to remove verity partitions: %s", strerror(-ret));
    }

    if ((ret = gpt_sync(gpt)) < 0)
    {
        ERR("failed to sync GUID partition table: %s", strerror(-ret));
    }

    gpt_close(gpt);
}

static void _create_cvmboot_cpio_archive(const char* disk, const char* signtool)
{
    buf_t buf = BUF_INITIALIZER;
    char efi_path[PATH_MAX];
    const char* source = efi_path;
    sig_t sig;
    const guid_t guid = efi_type_guid;

    if (find_gpt_entry_by_type(disk, &guid, efi_path, NULL) < 0)
        ERR("Cannot find EFI partition: %s", disk);

    if (mount(source, mntdir(), "vfat", 0, NULL) < 0)
        ERR("Failed to mount EFI directory: %s => %s", source, mntdir());

    // Create cvmboot.cpio and cvmboot.cpio.sig */
    {
        char cwd[PATH_MAX];
        char home[PATH_MAX];
        char cpio[PATH_MAX];
        char cpio_sig[PATH_MAX];
        void* cpio_data = NULL;
        size_t cpio_size = 0;

        paths_set_prefix("");
        paths_get(home, DIRNAME_CVMBOOT_HOME, mntdir());
        paths_get(cpio, FILENAME_CVMBOOT_CPIO, mntdir());
        paths_get(cpio_sig, FILENAME_CVMBOOT_CPIO_SIG, mntdir());
        paths_set_prefix("/boot/efi");

        if (!getcwd(cwd, sizeof(cwd)))
            ERR("failed to get the current working directory");

        if (chdir(home) < 0)
            ERR("failed to change directory to %s", home);

        execf(&buf, "find . | cpio --create --format='newc' > %s", cpio);

        if (chdir(cwd) < 0)
            ERR("failed to change directory to %s", cwd);

        if (access(cpio, R_OK) < 0)
            ERR("failed to create file: %s", cpio);

        // Load CPIO file into memory:
        if (load_file(cpio, &cpio_data, &cpio_size) < 0)
            ERR("failed to load CPIO file into memory: %s", cpio);

        // Create the verity signature structure
        if (sig_create(cpio_data, cpio_size, signtool, &sig) != 0)
            ERR("failed to create signature");

        // Write the file onto the EFI partition:
        if (write_file(cpio_sig, &sig, sizeof(sig)) < 0)
            ERR("failed to create file: %s", cpio_sig);

        printf("Created %s\n", strip_mntdir(cpio_sig));
        sig_dump_signer(&sig);

        free(cpio_data);
    }

    if (umount(mntdir()) < 0)
        ERR("failed to unmount: %s", mntdir());

    _dump_expected_pcr_and_log_contents(disk, &sig);

    buf_release(&buf);
}

static int _test_whether_prepared(const char* disk)
{
    int ret = 0;
    char path[PATH_MAX];
    char source[PATH_MAX];
    struct stat statbuf;
    char loop[PATH_MAX];

    losetup(disk, loop);

    if (find_gpt_entry_by_type(loop, &efi_type_guid, source, NULL) < 0)
        ERR("Cannot find EFI partition: %s", loop);

    if (mount(source, mntdir(), "vfat", 0, NULL) < 0)
        ERR("Failed to mount EFI directory: %s => %s", source, mntdir());

    paths_set_prefix("");
    paths_get(path, DIRNAME_CVMBOOT_HOME, mntdir());
    paths_set_prefix("/boot/efi");

    if (stat(path, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
        ret = -1;

    umount(mntdir());

    lodetach(loop);

    return ret;
}

static int _has_partition(const char* disk, const guid_t* type_guid)
{
    int ret = 0;
    int r = 0;
    gpt_t* gpt;

    if ((r = gpt_open(disk, O_RDONLY, &gpt)) < 0)
    {
        ERR("failed to open the GUID partition table: %s: %s",
            disk, strerror(-ret));
    }

    r = gpt_find_type_partition(gpt, type_guid);
    ret = (r < 0) ? -1 : 0;

    gpt_close(gpt);

    return ret;
}

static int _gpt_is_sorted(const char* disk)
{
    int ret = 0;
    int r = 0;
    gpt_t* gpt;

    if ((r = gpt_open(disk, O_RDONLY, &gpt)) < 0)
    {
        ERR("failed to open the GUID partition table: %s: %s",
            disk, strerror(-ret));
    }

    r = gpt_is_sorted(gpt);
    ret = (r < 0) ? -1 : 0;

    gpt_close(gpt);

    return ret;
}

typedef enum image_state
{
    IMAGE_STATE_BASE,
    IMAGE_STATE_PREPARED,
    IMAGE_STATE_PROTECTED,
    IMAGE_STATE_UNKNOWN,
}
image_state_t;

static void _fixup_gpt(const char* disk)
{
    gpt_t* gpt;

    if (gpt_open(disk, O_RDWR | O_EXCL, &gpt) < 0)
        ERR("%s(): failed to open GPT: %s\n", __FUNCTION__, disk);

    gpt_sync(gpt);
    gpt_close(gpt);
}

static image_state_t _get_image_state(const char* disk)
{
    image_state_t state = IMAGE_STATE_UNKNOWN;
    bool has_linux_partition = false;
    bool has_verity_partition = false;
    bool has_cvmboot_dir = false;

    // If GPT entries are incontiguous (with null gaps), then it must
    // be a base image since cvmdisk would have sorted the entries.
    if (_gpt_is_sorted(disk) < 0)
    {
        state = IMAGE_STATE_BASE;
        goto done;
    }

    if (_has_partition(disk, &linux_type_guid) == 0)
        has_linux_partition = true;

    if (_has_partition(disk, &verity_type_guid) == 0)
        has_verity_partition = true;

    if (_test_whether_prepared(disk) == 0)
        has_cvmboot_dir = true;

    if (has_cvmboot_dir || has_verity_partition)
    {
        state = IMAGE_STATE_PREPARED;
        goto done;
    }

    if (has_linux_partition)
    {
        state = IMAGE_STATE_BASE;
        goto done;
    }

done:
    return state;
}

static const char* _get_image_state_name(image_state_t state)
{
    switch (state)
    {
        case IMAGE_STATE_BASE:
            return "base";
        case IMAGE_STATE_PREPARED:
            return "prepared";
        case IMAGE_STATE_PROTECTED:
            return "protected";
        case IMAGE_STATE_UNKNOWN:
        default:
            return "unknown";
    };
}

static void _verify_disk(const char* disk)
{
    int ret = 0;
    gpt_t* gpt;
    uint32_t loopnum = 0;

    // Extract the loopback number (e.g., 4 from /dev/loop3p4)
    {
        uint32_t partnum;

        if (loop_parse(disk, &loopnum, &partnum) != 0)
        {
            ERR("invalid disk device name: %s", disk);
        }

        if (partnum != 0)
        {
            ERR("invalid disk device name: %s", disk);
        }
    }

    // Open the GUID partition table.
    if ((ret = gpt_open(disk, O_RDONLY, &gpt)) < 0)
    {
        ERR("failed to open the GUID partition table: %s: %s",
            disk, strerror(-ret));
    }

    // Iterate the GUID partition table, looking for verity partitions.
    {
        gpt_entry_t entries[GPT_MAX_ENTRIES];
        size_t num_entries;
        guid_t verity_type_guid;
        guid_init_str(&verity_type_guid, VERITY_PARTITION_TYPE_GUID);
        size_t num_verity_partitions = 0;

        gpt_get_entries(gpt, entries, &num_entries);

        for (size_t i = 0; i < num_entries; i++)
        {
            const gpt_entry_t* e = &entries[i];
            guid_t type_guid;
            guid_init_xy(&type_guid, e->type_guid1, e->type_guid2);

            // If this is a verity partition.
            if (guid_equal(&type_guid, &verity_type_guid))
            {
                blockdev_t* hdev = NULL;
                blockdev_t* ddev = NULL;
                char hpath[PATH_MAX];
                char dpath[PATH_MAX];
                uint32_t partnum = (uint32_t)(i + 1);
                const size_t block_size = VERITY_BLOCK_SIZE;
                sha256_t roothash;
                verity_superblock_t sb;
                size_t index;

                loop_format(hpath, loopnum, partnum);

                // Open the verity device.
                if (blockdev_open(hpath, O_RDONLY, 0, block_size, &hdev) != 0)
                {
                    ERR("failed to open hash device: %s", hpath);
                }

                // Get the roothash from the hash device.
                if (verity_get_roothash(hdev, &roothash) != 0)
                    ERR("failed to get roothash from %s", hpath);

                // Get the superblock form the hash device.
                if (verity_get_superblock(hdev, &sb) != 0)
                    ERR("failed to get superblock from %s", hpath);

                printf("%s>>> Verifying data partition...%s\n",
                    colors_green, colors_reset);

                verity_hashtree_t hashtree;

                printf("Loading verity hash tree...\n");

                // Load the hash tree into memory.
                if ((ret = verity_load_hash_tree(
                    hdev,
                    &sb,
                    &roothash,
                    &hashtree)) < 0)
                {
                    ERR("failed to load hash tree: %s: %s", hpath,
                        strerror(-ret));
                }

                guid_t unique_guid;
                guid_init_bytes(&unique_guid, sb.uuid);

                // Find data partition related to this hash partition.
                if ((index = gpt_find_partition(
                    gpt, &unique_guid)) == (size_t)-1)
                {
                    ERR("cannot find related data partition for %s", hpath);
                }

                loop_format(dpath, loopnum, index + 1);

                // Open the corresponding data device.
                if (blockdev_open(dpath, O_RDONLY, 0, block_size, &ddev) != 0)
                    ERR("failed to open data device: %s", dpath);

                if ((ret = verity_verify_data_device(
                    ddev,
                    &sb,
                    &roothash,
                    &hashtree)) < 0)
                {
                    ERR("Verify of data disk failed:  %s", dpath);
                }

                blockdev_close(hdev);
                blockdev_close(ddev);
                num_verity_partitions++;
            }
        }

        if (num_verity_partitions == 0)
            ERR("Disk contains no verity partitions");
    }

    // Close the GUID partition table.
    gpt_close(gpt);
}

static int _strip_disk(const char* disk, const char* vhd_file)
{
    size_t total_bytes = 0;
    buf_t buf = BUF_INITIALIZER;
    char loop[PATH_MAX]; /* vhd-file loopback device */
    gpt_entry_t entries[GPT_MAX_ENTRIES];
    size_t num_entries;
    gpt_entry_t entries0[GPT_MAX_ENTRIES]; /* backup of original */
    size_t rootfs_index;
    size_t upper_index;
    size_t num_rootfs_sectors = 0;
    const size_t one_gb = 1024 * 1024 * 1024;

    memset(&entries, 0, sizeof(entries));
    memset(&entries0, 0, sizeof(entries0));

    printf("%s>>> Stripping disk to create %s...%s\n",
        colors_green, vhd_file, colors_reset);

    /* Fixup the GPT */
    _fixup_gpt(disk);

    /* Refuse to strip disks that have no thin partitions */
    {
        if (find_gpt_entry_by_type(disk, &thin_data_type_guid, NULL, NULL) < 0)
            ERR("Refusing to strip disk that has no thin data partition");

        if (find_gpt_entry_by_type(disk, &thin_meta_type_guid, NULL, NULL) < 0)
            ERR("Refusing to strip disk that has no thin meta partition");
    }

    /* Compute total required bytes for the new VHD */
    printf("Computing required size for new vhd-file...\n");
    {
        gpt_t* gpt = NULL;

        /* get the index of the first linux partition (rootfs) */
        if ((rootfs_index = find_gpt_entry_by_type(
            disk, &linux_type_guid, NULL, NULL)) < 0)
        {
            ERR("Cannot find Linux root partition: disk=%s", disk);
        }

        /* get the index of the optional upper layer partition */
        upper_index = find_gpt_entry_by_type(
            disk, &rootfs_upper_type_guid, NULL, NULL);

        if (gpt_open(disk, O_RDONLY, &gpt) < 0)
            ERR("unable to open disk: %s", disk);

        gpt_get_entries(gpt, entries, &num_entries);
        memcpy(&entries0, &entries, sizeof(entries0));
        gpt_close(gpt);

        if (num_entries == 0)
            ERR("failed to get non-zero array of GPT entries");

        /* Adjust partitions starts/ends to account for removal of rootfs */
        for (size_t i = 0; i < num_entries; i++)
        {
            gpt_entry_t* e = &entries[i];
            const size_t num_sectors = e->ending_lba - e->starting_lba + 1;

            if (rootfs_index == i)
            {
                num_rootfs_sectors = num_sectors;
                /* The rootfs is stripped and not counted for final VHD */
                continue;
            }

            if (i > rootfs_index)
            {
                e->starting_lba -= num_rootfs_sectors;
                e->ending_lba -= num_rootfs_sectors;
            }
        }

        /* Calculate the bytes needed for the new vhd-file */
        {
            const gpt_entry_t* e = &entries[num_entries-1];
            total_bytes = e->ending_lba * GPT_BLOCK_SIZE;
        }

        /* Make room for the GPT primary and backup structures */
        total_bytes += sizeof(gpt->_primary);
        total_bytes += sizeof(gpt->_backup);

        /* Round to next gigabyte */
        total_bytes = round_up_to_multiple(total_bytes, one_gb);
    }

    /* Create the VHD file of the given size */
    {
        path_t vhd_path;
        cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

        /* remove vhd_file from previous run */
        execf(&buf, "rm -f %s.gz", vhd_file);
        execf(&buf, "rm -f %s", vhd_file);

        /* copy sample.vhd.gz to the vdh-file argument */
        printf("Creating %s.gz...\n", vhd_file);
        makepath2(&vhd_path, sharedir(), "sample.vhd.gz");
        execf(&buf, "cp %s %s.gz", vhd_path.buf, vhd_file);

        /* uncompress the file */
        printf("Uncompressing %s.gz...\n", vhd_file);
        execf(&buf, "gunzip %s.gz", vhd_file);

        /* set the file size in gigabytes */
        printf("Resizing %s to %zuGB...\n", vhd_file, total_bytes/one_gb);

        if (cvmvhd_resize(vhd_file, total_bytes, &err) < 0)
            ERR("%s", err.buf);
    }

    /* Setup loopback for vhd file */
    losetup(vhd_file, loop);
    execf(&buf, "sgdisk -e %s", loop);
    execf(&buf, "sgdisk -s %s", loop);

    /* Create partitions for new vhd-file */
    printf("Creating partitions for new vhd-file...\n");
    {
        gpt_t* gpt;

        if (gpt_open(loop, O_RDWR | O_EXCL, &gpt) < 0)
            ERR("failed to open the GUID partition table: %s", vhd_file);

        for (size_t i = 0; i < num_entries; i++)
        {
            gpt_entry_t entry = entries[i];
            guid_t guid;

            if (rootfs_index == i)
            {
                /* skip the partition that is being stripped */
                continue;
            }

            guid_init_xy(&guid, entry.type_guid1, entry.type_guid2);

            if (memcmp(&guid, &mbr_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding MBR partition...\n");
            }
            else if (memcmp(&guid, &efi_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding EFI partition...\n");
            }
            else if (memcmp(&guid, &rootfs_upper_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding rootfs upper-layer partition...\n");
            }
#ifdef USE_EFI_EPHEMERAL_DISK
            else if (memcmp(&guid, &efi_upper_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding EFI upper-layer partition...\n");
            }
#endif /* USE_EFI_EPHEMERAL_DISK */
            else if (memcmp(&guid, &thin_data_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding thin-data partition...\n");
            }
            else if (memcmp(&guid, &thin_meta_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding thin-meta partition...\n");
            }
            else if (memcmp(&guid, &verity_type_guid, sizeof(guid)) == 0)
            {
                printf("Adding verity partition...\n");
            }
            else
            {
                printf("Adding unknown partition...\n");
            }

            if (gpt_add_entry(gpt, &entry) < 0)
                ERR("failed to add GPT entry to %s", vhd_file);
        }

        gpt_close(gpt);
    }

    /* detach loopback device for new VHD if doing sparse copy */
    {
        lodetach(loop);
        *loop = '\0';

        lodetach(globals.loop);
        *globals.loop = '\0';
    }

    /* Copy partitions from disk to new vhd-file */
    for (size_t i = 0; i < num_entries; i++)
    {
        /* skip copying of rootfs and of upper layer */
        if (rootfs_index == i)
            continue;

        if (upper_index == i)
            continue;

        {
            const gpt_entry_t e0 = entries0[i];
            size_t offset0 = gpt_entry_offset(&e0);
            size_t end0 = offset0 + gpt_entry_size(&e0);
            const gpt_entry_t e = entries[i];
            size_t offset = gpt_entry_offset(&e);
            frag_list_t frags = FRAG_LIST_INITIALIZER;
            frag_list_t holes = FRAG_LIST_INITIALIZER;
            char msg[1024];
            size_t j = i;

            if (i > rootfs_index)
                j--;

            if (frags_find(globals.disk, offset0, end0, &frags, &holes) < 0)
                ERR("frags_find() failed: %s", globals.disk);

            snprintf(msg, sizeof(msg), "Copying partition %zu => %zu", i, j);

            if (frags_copy(
                &frags, globals.disk, offset0, vhd_file, offset, msg) < 0)
            {
                ERR("frags_copy failed(): %s => %s", globals.disk, vhd_file);
            }

            frags_release(&frags);
            frags_release(&holes);
        }
    }

    buf_release(&buf);

    if (*loop)
        lodetach(loop);

    return 0;
}

/* assumes loop device is already setup for disk */
static void _strip_disk_in_place(const char* disk)
{
    char fullpath[PATH_MAX];
    char tmpfile[PATH_MAX];

    if (!realpath(globals.disk, fullpath))
        ERR("failed to resolve full path of %s", globals.disk);

    strlcpy2(tmpfile, fullpath, "_XXXXXX", sizeof(tmpfile));

    if (mkstemp(tmpfile) < 0)
        ERR("failed to create temporary file: %s", tmpfile);

    _strip_disk(disk, tmpfile);

    if (*globals.loop)
    {
        lodetach(globals.loop);
        *globals.loop = '\0';
    }

    printf("Moving %s => %s...\n", tmpfile, globals.disk);
    unlink(globals.disk);

    if (link(tmpfile, globals.disk) < 0)
        ERR("link(%s, %s) failed", tmpfile, globals.disk);

    unlink(tmpfile);
}

void _protect_disk(const char* disk, const char* signtool, bool verify)
{
    sha256_t roothash;
    sha256_string_t str;

    if (access(disk, F_OK) != 0)
        ERR("cannot access %s", disk);

    // Compute the roothash from the hash device (created during "prepare")
    {
        blockdev_t* dev = NULL;
        char path[PATH_MAX];

        if (find_gpt_entry_by_type(disk, &verity_type_guid, path, NULL) < 0)
            ERR("Cannot find verity partition: disk=%s", disk);

        if (blockdev_open(path, O_RDONLY, 0, VERITY_BLOCK_SIZE, &dev) != 0)
            ERR("failed to open hash device: %s", path);

        if (verity_get_roothash(dev, &roothash) < 0)
            ERR("failed to get root hash from device");

        blockdev_close(dev);
    }

    sha256_format(&str, &roothash);
    printf("%sroothash: %s%s\n", colors_cyan, str.buf, colors_reset);

    // Create the cvmboot CPIO archive on the EFI partition
    _create_cvmboot_cpio_archive(disk, signtool);

    if (verify)
        _verify_disk(disk);
}

/* ATTN: move to its own file: parsing.c? */
static int _is_valid_username(const char* s)
{
    const char* p = s;

    /* skip over valid first characters */
    if (!(*p == '_' || (*p >= 'a' && *p <= 'z')))
        return 0;

    p++;

    /* skip over valid inner characters */
    while (*p == '_' || (*p >= 'a' && *p <= 'z') || isdigit(*p))
        p++;

    /* usernames can end in '$' */
    if (*p == '$')
        p++;

    /* fail if any characters remain */
    if (*p)
        return 0;

    /* usernames are limited to 32 characters */
    if ((p - s) > 32)
        return 0;

    return 1;
}

/* ATTN: move to its own file: parsing.c? */
static int _is_valid_hostname(const char* s)
{
    const char* p = s;

    /* skip over valid first characters */
    if (!(*p >= 'a' && *p <= 'z'))
        return 0;

    p++;

    /* skip over valid inner characters */
    while (*p == '.' || *p == '-' || (*p >= 'a' && *p <= 'z') || isdigit(*p))
        p++;

    /* fail if any characters remain */
    if (*p)
        return 0;

    /* hostnames are limited to 253 characters */
    if ((p - s) > 253)
        return 0;

    return 1;
}

static void _get_user_option(int* argc, const char* argv[], user_opt_t* user)
{
    const char* opt;
    err_t err;

    *user->username = '\0';
    *user->password = '\0';
    *user->sshkey = '\0';

    if (getoption(argc, argv, "--user", &opt, &err) == 0)
    {
        char* p;

        if ((p = strchr(opt, ':')))
            *p++ = '\0';

        strlcpy(user->username, opt, sizeof(user->username));

        if (p)
        {
            const char* start = p;

            if ((p = strchr(start, ':')))
                *p++ = '\0';

            strlcpy(user->password, start, sizeof(user->password));
        }

        if (p)
        {
            const char* start = p;

            if ((p = strchr(start, ':')))
                *p++ = '\0';

            strlcpy(user->sshkey, start, sizeof(user->sshkey));
        }
    }

    if (*user->username && !_is_valid_username(user->username))
        ERR("bad username for --user option: \"%s\"", user->username);

    if (*user->password && access(user->password, R_OK) != 0)
        ERR("missing password file for --user option: \"%s\"", user->password);

    if (*user->sshkey && access(user->sshkey, R_OK) != 0)
        ERR("missing ssh-key file for --user option: \"%s\"", user->sshkey);
}

static int _create_cvmsign_public_private_keys(
    const char* private_key_path,
    const char* public_key_path)
{
    int ret = -1;
    void* private_data = NULL;
    size_t private_size;
    void* public_data = NULL;
    size_t public_size;
    bool found_private = false;
    bool found_public = false;
    const char PRIVATE[] = "-----BEGIN PRIVATE KEY-----";
    const char PUBLIC[] = "-----BEGIN PUBLIC KEY-----";
    char home[PATH_MAX];
    char confdir[PATH_MAX];
    buf_t buf = BUF_INITIALIZER;
    char private_path[PATH_MAX];
    char public_path[PATH_MAX];
    uid_t uid = 0;
    gid_t gid = 0;

    /* check for the private key */
    if (load_file(private_key_path, &private_data, &private_size) == 0 &&
        strstr((const char*)private_data, PRIVATE))
    {
        found_private = true;
    }

    /* check for the public key */
    if (load_file(public_key_path, &public_data, &public_size) == 0 &&
        strstr((const char*)public_data, PUBLIC))
    {
        found_public = true;
    }

    /* if both keys not found, then bail out */
    if (!(found_private && found_public))
        goto done;

    if (sudo_get_uid_gid(&uid, &gid) < 0)
        ERR("failed to get user's uid/gid");

    if (sudo_get_home_dir(home) < 0)
        ERR("failed to get user's home directory");

    strlcpy2(confdir, home, "/.cvmsign", PATH_MAX);
    strlcpy2(private_path, confdir, "/private.pem", PATH_MAX);
    strlcpy2(public_path, confdir, "/public.pem", PATH_MAX);

    if (execf_return(&buf, "mkdir -p %s", confdir) < 0)
        ERR("failed to create directory: %s", confdir);

    if (write_file(private_path, private_data, private_size) < 0)
        ERR("failed to create file: %s", private_path);

    if (write_file(public_path, public_data, public_size) < 0)
        ERR("failed to create file: %s", public_path);

    execf(&buf, "chmod 600 %s", private_path);
    execf(&buf, "chown -R %u.%u %s", uid, gid, confdir);

    ret = 0;

done:

    free(private_data);
    free(public_data);
    buf_release(&buf);

    return ret;
}

static void _add_verity_partition(const char* disk, bool verify)
{
    err_t err = ERR_INITIALIZER;
    buf_t buf = BUF_INITIALIZER;
    char linux_path[PATH_MAX];
    sha256_t roothash;

    if (access(disk, F_OK) != 0)
        ERR("cannot access %s", disk);

    /* Find the Linux rootfs partition */
    if (find_gpt_entry_by_type(disk, &linux_type_guid, linux_path, NULL) < 0)
        ERR("Cannot find Linux rootfs partition: %s", disk);

    // Add the verity partition for the rootfs */
    {
        guid_t unique_guid;
        const bool trace = true;
        const bool progress = true;
        int ret = 0;

        memset(&roothash, 0, sizeof(roothash));

        if ((ret = verity_add_partition(
            disk,
            linux_path,
            trace,
            progress,
            &unique_guid,
            &roothash,
            &err)) != 0)
        {
            ERR("%s: %s", err.buf, strerror(-ret));
        }

        // verity_add_partition() reassigns the loop device
        disk = globals.loop;
    }

    /* Add roothash to the cvmboot.conf file */
    {
        char path[PATH_MAX];
        char conf_path[PATH_MAX];

        if (find_gpt_entry_by_type(disk, &efi_type_guid, path, NULL) < 0)
            ERR("Cannot find EFI partition: %s", disk);

        if (mount(path, mntdir(), "vfat", 0, NULL) < 0)
            ERR("Failed to mount EFI directory: %s => %s", path, mntdir());

        paths_set_prefix("");
        paths_get(conf_path, FILENAME_CVMBOOT_CONF, mntdir());
        paths_set_prefix("/boot/efi");

        // Find and add hash of root filesystem partition to 'rootfs' file:
        {
            sha256_string_t str;
            sha256_format(&str, &roothash);
            execf(&buf, "sed -i '/^roothash=/d' %s", conf_path);
            execf(&buf, "echo 'roothash=%s' >> %s", str.buf, conf_path);
        }

        umount(mntdir());
    }

    buf_release(&buf);
}

static void _prepare_disk(
    const char* disk,
    const user_opt_t* user,
    const hostname_opt_t* hostname,
    const char* events,
    bool skip_resolv_conf,
    bool use_resource_disk,
    bool use_thin_provisioning,
    bool verify,
    bool expand_root_partition,
    bool no_strip)
{
    char version[PATH_MAX] = "";

    /* check the validity of the events file */
    if (events)
    {
        sha256_t signer = SHA256_INITIALIZER;

        /* use a zero-valued signer */
        sha256_string_t str;
        sha256_format(&str, &signer);
        preprocess_events(events, str.buf);
    }

    // Remove extra partitions
    _purge_disk(disk, true, true);

    // remove the cvmboot directory from the ESP
    _remove_cvmboot_dir(disk);

    // Expand the EXT4 to use most of disk:
    if (expand_root_partition)
        _expand_ext4_root_partition(disk);
    else
        _round_root_partition(disk);

    // Patch the fstab file to enable noatime:
    _patch_fstab(disk);

    // Disable Azure resource-disk formatting and mounting on /mnt:
    if (use_resource_disk)
        _preserve_resource_disk(disk);

    // Update resolv.conf so apt commands will work below:
    if (!skip_resolv_conf)
        _update_resolv_conf(disk);

    // Remove the KVP service:
    _remove_kvp_service(disk);

    // Remove cvmboot.conf if any:
    _remove_cvmboot_conf(disk);

    // Install the kernel onto the EFI partition:
    _install_kernel_onto_esp(disk, version);

    // Install the initrd onto the EFI partition:
    _install_initrd_onto_esp(disk, version, use_resource_disk,
        use_thin_provisioning);

    // Install the bootloader onto the EFI partition:
    _install_bootloader(disk, events);

    // Install Linux cmdline file onto EFI partition:
    _append_cmdline_option(disk, version);

    // Add user:
    if (*user->username)
        _add_user(disk, user);

    // Set hostname:
    if (*hostname->buf)
        _set_hostname(disk, hostname->buf);

    // Purge any extra partition created before:
    _purge_disk(disk, true, true);

    _add_extra_partitions(disk, use_thin_provisioning, use_resource_disk,
        verify);

    // Add the verity partition for the rootfs
    _add_verity_partition(disk, verify);

    // Remove uneededpartitions:
    if (!no_strip)
        _strip_disk_in_place(disk);
}

/*
**==============================================================================
**
** subcommands:
**
**==============================================================================
*/

#define PREPARE_USAGE "\n\
Usage: %s %s [options] <input-disk> <output-disk>\n\
\n\
Synopsis:\n\
    Prepares a VM disk image for integrity protection.\n\
\n\
Options:\n\
    --user=<username>:<password-file>:<ssh-key-file>\n\
        Adds a user account to the VM image.\n\
    --hostname=<hostname>\n\
        Sets the hostname of the VM image.\n\
    --events=<tpm-events-file>\n\
        Specifies custom events that will later be extended to PCRs and added\n\
        to the TCG log by the cvmboot boot loader.\n\
    --use-resource-disk\n\
        Use the Azure local ephemeral resource disk as the writeable,\n\
        upper-layer rootfs partition (rather than placing it on the VM disk\n\
        image).\n\
    --skip-resolv-conf\n\
        Do not attempt to patch the /etc/resolv.conf file.\n\
    --no-thin-provisioning\n\
        Do not use thin provisioning.\n\
    --expand-root-partition\n\
        Expand the EXT4 rootfs partition to consume the entire disk.\n\
    --verify\n\
        Verify that the newly-created thin partition matches the original\n\
        rootfs partition.\n\
    --no-strip\n\
        Do not strip the EXT4 rootfs partition.\n\
\n\
Description:\n\
    This subcommand prepares a VM disk image for integrity protection by\n\
    performing the following tasks.\n\
\n\
    (*) Arranges for the rootfs partition to be mounted as read-only by\n\
        updating /etc/fstab.\n\
    (*) Removes the Hyper-V data exchange service (KVP).\n\
    (*) Installs the cvmboot boot loader onto the EFI system partition as\n\
        /boot/efi/EFI/BOOT/BOOTX64.EFI.\n\
    (*) Updates initrd to handle disk integrity protection and optionally\n\
        thin provisioning.\n\
    (*) Optionally patches /etc/resolv.conf.\n\
    (*) Optionally creates the thin meta/data partitions.\n\
    (*) Optionally creates the writable, upper-layer, rootfs partition\n\
        (known as the dm-snapshot copy-on-write device).\n\
    (*) Optionally adds a user account (see --username).\n\
    (*) Optionally sets the hostname (see --hostname).\n\
    (*) Creates the /boot/efi/EFI/cvmboot directory, adding the following\n\
        files: cvmboot.conf, events, initrd, and vmlinuz.\n\
\n\
    After a disk has been prepared, it can be further customized by the\n\
    VM owner using whatever means. The final step is to protect the disk\n\
    using the 'cvmdisk protect' subcommand.\n\
\n\
Notes:\n\
    <password-file> -- generated by mkpasswd command.\n\
    <ssh-key-file>  -- public SSH key file (e.g., ~/.ssh/id_rsa.pub)\n\
\n"
static int _subcommand_prepare(
    int argc,
    const char* argv[],
    const user_opt_t* user,
    const hostname_opt_t* hostname,
    const char* events,
    bool skip_resolv_conf,
    bool use_resource_disk,
    bool use_thin_provisioning,
    bool verify,
    bool expand_root_partition,
    bool no_strip)
{
    const char* input_disk = NULL;
    const char* output_disk = NULL;
    const char* disk = NULL;

    if (argc != 4)
    {
        printf(PREPARE_USAGE, argv[0], argv[1]);
        exit(1);
    }

    input_disk = argv[2];
    output_disk = argv[3];
    _check_vhd(input_disk);

    if (_same_file(input_disk, output_disk))
    {
        ERR("input-disk and output-disk refer to the same file: %s %s\n",
            input_disk, output_disk);
    }

    switch (_get_image_state(input_disk))
    {
        case IMAGE_STATE_BASE:
            break;
        case IMAGE_STATE_PREPARED:
            ERR("disk has already been prepared: %s", input_disk);
        case IMAGE_STATE_PROTECTED:
            ERR("disk has already been protected: %s", input_disk);
        case IMAGE_STATE_UNKNOWN:
            ERR("unknown disk state: %s", input_disk);
    }

    if (sparse_copy(input_disk, output_disk) < 0)
        ERR("copy failed: %s => %s\n", input_disk, output_disk);

    globals.disk = output_disk;
    losetup(globals.disk, globals.loop);
    disk = globals.loop;
    atexit(_atexit_function);

    _fixup_gpt(disk);

    _prepare_disk(disk, user, hostname, events, skip_resolv_conf,
        use_resource_disk, use_thin_provisioning, verify,
        expand_root_partition, no_strip);

    return 0;
}

#define PROTECT_USAGE "\n\
Usage: %s %s [options] <disk> <signing-tool>\n\
\n\
Synopsis:\n\
    Protects a VM disk image with verity protection and digital signing.\n\
\n\
Options:\n\
    --verify\n\
        Verify the verity and thin partitions.\n\
\n\
Description:\n\
    The protect subcommand protects the VM disk image after it has been\n\
    prepared by the 'cvmdisk prepare' subcommand. The following tasks are\n\
    performed.\n\
    \n\
    (*) Injects the verity partition, which provides a hash-device that fully\n\
        measures the rootfs partition.\n\
    (*) Adds the root hash of the verity partition to cvmboot configuration\n\
        (/boot/efi/EFI/cvmboot/cvmboot.cpio).\n\
    (*) Creates the cvmboot.cpio archive from the contents of the cvmboot\n\
        directory (/boot/efi/EFI/cvmboot directory).\n\
    (*) Digitally signs the cvmboot.cpio file and stores the signature on\n\
        the EFI system partition (/boot/efi/EFI/cvmboot/cvmboot.cpio.sig).\n\
    (*) Removes (strips) the original EXT4 rootfs partition from the VM disk\n\
        image (unless the --no-strip option is present).\n\
\n\
    The resulting VM disk image is ready for deployment.\n\
\n\
\n"
static int _subcommand_protect(int argc, const char* argv[], bool verify)
{
    const char* disk = NULL;
    buf_t buf = BUF_INITIALIZER;
    const char* signtool;
    char signtool_path[PATH_MAX];

    if (argc != 4)
    {
        printf(PROTECT_USAGE, argv[0], argv[1]);
        exit(1);
    }

    _check_vhd(argv[2]);
    _setup_loopback(argc, argv);

    disk = argv[2];
    signtool = argv[3];

    if (access(disk, F_OK) != 0)
        ERR("cannot access %s", disk);

    switch (_get_image_state(globals.disk))
    {
        case IMAGE_STATE_BASE:
            ERR("disk has not been prepared yet: %s", globals.disk);
        case IMAGE_STATE_PREPARED:
            break;
        case IMAGE_STATE_PROTECTED:
            ERR("disk has already been protected: %s", globals.disk);
        case IMAGE_STATE_UNKNOWN:
            ERR("unknown disk state: %s", globals.disk);
    }

    /* Locate the signing tool */
    if (_locate_signtool(signtool, signtool_path) < 0)
        ERR("unable to locate signing tool: %s", signtool);

    /* Peform a test signing of the signing tool */
    _test_signtool(signtool_path);

    // Move the secondary GPT header to the end of the device:
    execf(&buf, "sgdisk -e %s", disk);

    // Sort the partitions
    execf(&buf, "sgdisk -s %s", disk);

    // Create the verity partitions:
    _protect_disk(disk, signtool_path, verify);

    buf_release(&buf);

    return 0;
}

#define INIT_USAGE "\n\
Usage: %s %s [options] <input-disk> <output-disk> signing-tool\n\
\n\
Synopsis:\n\
    Both prepares and protects a VM disk image.\n\
\n\
Options:\n\
    --user=<username>:<password-file>:<ssh-key-file>\n\
        Adds a user account to the VM image.\n\
    --hostname=<hostname>\n\
        Sets the hostname of the VM image.\n\
    --events=<tpm-events-file>\n\
        Specifies custom events that will later be extended to PCRs and added\n\
        to the TCG log by the cvmboot boot loader.\n\
    --use-resource-disk\n\
        Use the Azure local ephemeral resource disk as the writeable,\n\
        upper-layer rootfs partition (rather than placing it on the VM disk\n\
        image).\n\
    --skip-resolv-conf\n\
        Do not attempt to patch the /etc/resolv.conf file.\n\
    --no-thin-provisioning\n\
        Do not use thin provisioning.\n\
    --expand-root-partition\n\
        Expand the EXT4 rootfs partition to consume the entire disk.\n\
    --verify\n\
        Verify the verity and thin partitions.\n\
    --no-strip\n\
        Do not strip the EXT4 rootfs partition.\n\
\n\
Description:\n\
    This subcommand both prepares and protects a VM disk image. It is\n\
    equivalent to running 'cvmdisk prepare' followed by 'cvmdisk protect'.\n\
    For more details see the prepare and protect subcommands.\n\
\n\
\n"
static int _subcommand_init(
    int argc,
    const char* argv[],
    const user_opt_t* user,
    const hostname_opt_t* hostname,
    const char* events,
    bool delta,
    bool skip_resolv_conf,
    bool use_resource_disk,
    bool use_thin_provisioning,
    bool verify,
    bool expand_root_partition,
    bool no_strip)
{
    const char* input_disk = NULL;
    const char* output_disk = NULL;
    const char* disk = NULL;
    const char* signtool = NULL;
    char signtool_path[PATH_MAX];

    /* check the arguments */
    if (argc != 5)
    {
        printf(INIT_USAGE, argv[0], argv[1]);
        exit(1);
    }

    input_disk = argv[2];
    output_disk = argv[3];
    signtool = argv[4];
    _check_vhd(input_disk);

    if (_same_file(input_disk, output_disk))
    {
        ERR("input-disk and output-disk refer to the same file: %s %s\n",
            input_disk, output_disk);
    }

    switch (_get_image_state(input_disk))
    {
        case IMAGE_STATE_BASE:
            break;
        case IMAGE_STATE_PREPARED:
            ERR("disk has already been prepared: %s", input_disk);
        case IMAGE_STATE_PROTECTED:
            ERR("disk has already been protected: %s", input_disk);
        case IMAGE_STATE_UNKNOWN:
            ERR("unknown disk state: %s", input_disk);
    }

    if (sparse_copy(input_disk, output_disk) < 0)
        ERR("copy failed: %s => %s\n", input_disk, output_disk);

    globals.disk = output_disk;
    losetup(globals.disk, globals.loop);
    disk = globals.loop;
    atexit(_atexit_function);

    if (access(disk, F_OK) != 0)
        ERR("cannot access %s", disk);

    /* Verify that signing tool is available */
    if (_locate_signtool(signtool, signtool_path) < 0)
        ERR("unable to locate signing tool: %s", signtool);

    /* Peform a test signing of the signing tool */
    _test_signtool(signtool_path);

    // Fixup the GPT info:
    _fixup_gpt(disk);

    _prepare_disk(disk, user, hostname, events, skip_resolv_conf,
        use_resource_disk, use_thin_provisioning, verify,
        expand_root_partition, no_strip);

    // Protect the disk:
    globals.disk = output_disk;
    losetup(globals.disk, globals.loop);
    disk = globals.loop;
    _protect_disk(disk, signtool_path, verify);

    return 0;
}

#define SHELL_USAGE "\n\
Usage: %s %s [options] <disk>\n\
\n\
Synopsis:\n\
    Establish a Bash command shell with a VM disk image.\n\
\n\
Options:\n\
    --read-only, --ro\n\
        Establish read-only session.\n\
    --no-bind\n\
        Do not perform bind mounts of /dev, /proc, and /sys.\n\
\n\
Description:\n\
    This subcommand establishes a Bash command shell with a VM disk image.\n\
    From a shell session, it is possible to perform most tasks, such as\n\
    adding users, installing software, and changing system configuation.\n\
    Shell sessions must be establihed before/after images are prepared (with\n\
    cvmdisk prepare) and before they are protected (cvmdisk protect).\n\
\n\
\n"
static int _subcommand_shell(
    int argc,
    const char* argv[],
    bool read_only,
    bool nobind)
{
    char part[PATH_MAX];
    buf_t buf = BUF_INITIALIZER;

    if (argc < 3)
    {
        printf(SHELL_USAGE, argv[0], argv[1]);
        exit(1);
    }

    _check_vhd(argv[2]);
    _setup_loopback(argc, argv);

    const char* disk = argv[2];
    execf(&buf, "sgdisk -e %s", disk);
    execf(&buf, "sgdisk -s %s", disk);

    if (find_gpt_entry_by_type(disk, &linux_type_guid, part, NULL) < 0 ||
        __test_ext4_rootfs(part) < 0)
    {
        printf(
            "Cannot shell into disk image since it has no EXT4 rootfs "
            "partition. Perhaps this image has already been protected "
            "and therefore stripped of its rootfs partition.\n");
        exit(1);
    }

    /* Mount the root file system */
    mount_disk_ex(disk, read_only ? MS_RDONLY : 0, !nobind);

    // Shell into rootfs:
    {
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof(cmd), "chroot %s /bin/bash", mntdir());
        if (system(cmd))
            ;
    }

    /* Unmount the root file system */
    umount_disk();
    buf_release(&buf);

    return 0;
}

/*============================================================================*/

static int _subcommand_gpt(int argc, const char* argv[])
{
    const char* path;
    gpt_t* gpt = NULL;
    int ret;

    // Check argument count.
    if (argc != 3)
    {
        printf("Usage: %s %s <device>\n", argv[0], argv[1]);
        exit(1);
    }

    // Collect arguments.
    path = argv[2];

    // Open the GUID partition table.
    if ((ret = gpt_open(path, O_RDONLY, &gpt)) < 0)
    {
        ERR("failed to open the GUID partition table: %s: %s",
            path, strerror(-ret));
    }

    // Dump all GPT entries.
    if (g_options.verbose)
        gpt_dump(gpt);
    else
    {
        printf("\n");
        gpt_dump_concise(gpt);
        printf("\n");
    }

    return 0;
}

static int _subcommand_expand_root_partition(int argc, const char* argv[])
{
    const char* disk = NULL;

    if (argc != 3)
    {
        printf("Usage: %s %s <disk>\n", argv[0], argv[1]);
        exit(1);
    }

    _check_vhd(argv[2]);
    _setup_loopback(argc, argv);

    disk = argv[2];

    _fixup_gpt(disk);
    _expand_ext4_root_partition(disk);

    return 0;
}

static int _subcommand_genkeys(int argc, const char* argv[])
{
    if (argc != 4)
    {
        printf("Usage: %s %s <private-keyfile> <public-keyfile>\n",
            argv[0], argv[1]);
        exit(1);
    }

    const char* private_keyfile = argv[2];
    const char* public_keyfile = argv[3];

    _genkeys(private_keyfile, public_keyfile);
    printf("Created %s\n", private_keyfile);
    printf("Created %s\n", public_keyfile);

    return 0;
}

static int _subcommand_fixgpt(int argc, const char* argv[])
{
    const char* disk = NULL;

    if (argc != 3)
    {
        printf("Usage: %s %s <disk>\n", argv[0], argv[1]);
        exit(1);
    }

    disk = argv[2];

    if (access(disk, F_OK) != 0)
        ERR("cannot access %s", disk);

    _fixup_gpt(disk);

    return 0;
}

#if 0
static int _subcommand_compare_partitions(int argc, const char* argv[])
{
    const char* filename1;
    const char* filename2;
    struct stat statbuf1;
    struct stat statbuf2;
    gpt_entry_t entries1[GPT_MAX_ENTRIES];
    gpt_entry_t entries2[GPT_MAX_ENTRIES];
    size_t num_entries1 = 0;
    size_t num_entries2 = 0;
    int fd1;
    int fd2;

    if (argc != 4)
    {
        printf("Usage: %s %s <filename> <filename>\n", argv[0], argv[1]);
        exit(1);
    }

    filename1 = argv[2];
    filename2 = argv[2];

    if (stat(filename1, &statbuf1) < 0)
        ERR("failed to stat %s", filename1);

    if (stat(filename2, &statbuf2) < 0)
        ERR("failed to stat %s", filename2);

    if (statbuf1.st_size != statbuf2.st_size)
        ERR("fails are different sizes");

    memset(&entries1, 0, sizeof(entries1));
    memset(&entries2, 0, sizeof(entries2));

    /* get entries1[] */
    {
        gpt_t* gpt = NULL;

        if (gpt_open(filename1, O_RDONLY, &gpt) < 0)
            ERR("unable to open disk: %s", filename1);

        gpt_get_entries(gpt, entries1, &num_entries1);
        gpt_close(gpt);
    }

    /* get entries2[] */
    {
        gpt_t* gpt = NULL;

        if (gpt_open(filename2, O_RDONLY, &gpt) < 0)
            ERR("unable to open disk: %s", filename2);

        gpt_get_entries(gpt, entries2, &num_entries2);
        gpt_close(gpt);
    }

    if (num_entries1 != num_entries2)
        ERR("GPT tables have different number of entries");

    if ((fd1 = open(filename1, O_RDONLY)) < 0)
        ERR("failed to open %s", filename1);

    if ((fd2 = open(filename2, O_RDONLY)) < 0)
        ERR("failed to open %s", filename2);

    /* Compare each partition */
    for (size_t i = 0; i < num_entries2; i++)
    {
        gpt_entry_t e1 = entries1[i];
        gpt_entry_t e2 = entries2[i];
        size_t offset = gpt_entry_offset(&e1);
        size_t size = gpt_entry_size(&e1);
        const size_t bs = 4096;
        size_t num_blocks = size / bs;
        progress_t progress;
        const char msg[] = "Comparing";

        printf("Comparing partition %zu...\n", i);

        if (e1.type_guid1 != e2.type_guid1)
            ERR("type_guid1 mismatch");

        if (e1.type_guid2 != e2.type_guid2)
            ERR("type_guid2 mismatch");

        if (e1.starting_lba != e2.starting_lba)
            ERR("starting_lba mismatch");

        if (e1.ending_lba != e2.ending_lba)
            ERR("starting_lba mismatch");

        if (e1.attributes != e2.attributes)
            ERR("attributes mismatch");

        if (memcmp(e1.type_name, e2.type_name, sizeof(e1.type_name)) != 0)
            ERR("type_name mismatch");

        progress_start(&progress, msg);

        for (size_t j = 0; j < num_blocks; j++)
        {
            uint8_t buf1[bs];
            uint8_t buf2[bs];
            const off_t off = offset + (j * bs);

            if (pread(fd1, buf1, bs, off) != bs)
                ERR("read failed on partition %zu", i);

            if (pread(fd2, buf2, bs, off) != bs)
                ERR("read failed on partition %zu", i);

            if (memcmp(buf1, buf2, bs) != 0)
                ERR("comparison failed on partition %zu", i);

            progress_update(&progress, j, num_blocks);
        }

        progress_end(&progress);
    }

    close(fd1);
    close(fd2);

    return 0;
}
#endif

static int _subcommand_state(int argc, const char* argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s %s <disk>\n", argv[0], argv[1]);
        exit(1);
    }

    _check_vhd(argv[2]);
    _setup_loopback(argc, argv);

    const char* disk = argv[2];

    image_state_t state = _get_image_state(disk);

    if (state == IMAGE_STATE_UNKNOWN)
        ERR("unable to determine state of image: %s", globals.disk);

    printf("%s: %s image\n", globals.disk, _get_image_state_name(state));

    return 0;
}

static int _subcommand_digest(int argc, const char* argv[])
{
    sha256_t hash;
    sha256_string_t str;

    if (argc != 3)
    {
        printf("Usage: %s %s <path>\n", argv[0], argv[1]);
        exit(1);
    }

    const char* path = argv[2];

    if (sparse_shasha256(path, &hash) < 0)
        ERR("Cannot find hash of file: %s", path);

    sha256_format(&str, &hash);

    printf("%s\n", str.buf);

    return 0;
}

static int _subcommand_copy(int argc, const char* argv[])
{
    int ret = 0;

    if (argc != 4)
    {
        printf("Usage: %s %s <input-file> <output-file>\n", argv[0], argv[1]);
        exit(1);
    }

    const char* infile = argv[2];
    const char* outfile = argv[3];

    if (sparse_copy(infile, outfile) < 0)
        ERR("copy failed: %s => %s\n", infile, outfile);

    return ret;
}

static int _subcommand_azcopy(int argc, const char* argv[])
{
    int ret = 0;
    buf_t buf = BUF_INITIALIZER;
    char dirname_buf[PATH_MAX];
    char basename_buf[PATH_MAX];
    const char* bn;
    const char* dn;
    char tmpdir[PATH_MAX];
    char basedir[PATH_MAX];
    char mntdir[PATH_MAX];
    char azcopy[PATH_MAX];

    if (argc != 4)
    {
        printf("Usage: %s %s <url> <filename>\n", argv[0], argv[1]);
        exit(1);
    }

    const char* url = argv[2];
    const char* filename = argv[3];

    /* Get dirname/basename of filename */
    strlcpy(dirname_buf, filename, sizeof (dirname_buf));
    strlcat(basename_buf, filename, sizeof (basename_buf));
    dn = dirname(dirname_buf);
    bn = basename(basename_buf);

    /* format tmpdir name */
    strlcpy(tmpdir, dn, sizeof(tmpdir));
    strlcat(tmpdir, "/.", sizeof(tmpdir));
    strlcat(tmpdir, bn, sizeof(tmpdir));
    strlcat(tmpdir, ".tmpdir", sizeof(tmpdir));

    /* format basedir name */
    strlcpy2(basedir, tmpdir, "/basedir", sizeof(basedir));

    /* format mntdir name */
    strlcpy2(mntdir, tmpdir, "/mntdir", sizeof(mntdir));

    /* Remove tmpdir */
    execf(&buf, "rm -rf %s", tmpdir);

    /* Locate the full path of the azcopy utility */
    if (which("azcopy", azcopy) != 0)
        ERR("failed to locate the azcopy program");

    /* Create directories */
    {
        if (mkdir(tmpdir, 0755) < 0)
            ERR("failed to create directory: %s", tmpdir);

        if (mkdir(basedir, 0755) < 0)
            ERR("failed to create directory: %s", basedir);

        if (mkdir(mntdir, 0755) < 0)
            ERR("failed to create directory: %s", mntdir);
    }

    /* Start the sparsefs driver */
    execf(&buf, "sparsefs-mount %s %s", basedir, mntdir);

    /* Run azcopy */
    {
        char cmd[3*PATH_MAX];

        snprintf(cmd, sizeof(cmd), "%s copy \"%s\" %s/%s",
            azcopy, url, mntdir, bn);

        if (system(cmd) != 0)
        {
            fprintf(stderr, "Command failed: '%s'", cmd);
            ret = 1;
        }
    }

    /* Stop the sparsefs driver */
    execf(&buf, "fusermount -u %s", mntdir);

    /* Move the new file from the base directory to actual name */
    if (ret == 0)
    {
        execf(&buf, "rm -f %s", filename);
        execf(&buf, "mv %s/%s %s", basedir, bn, filename);
        printf("Created %s\n", filename);
    }

    /* Remove the temporary directory */
    execf(&buf, "rm -rf %s", tmpdir);

    /* Print file stats */
    {
        struct stat statbuf;

        if (stat(filename, &statbuf) < 0)
            ERR("failed to stat %s", filename);

        size_t sparse_size = statbuf.st_blocks * 512;
        size_t apparent_size = statbuf.st_size;

        printf("File is %5.2lf%% sparse\n",
            (1.0 - ((double)sparse_size / (double)apparent_size)) * 100.0);
    }

    buf_release(&buf);

    return ret;
}

const char USAGE[] = "\n\
Usage: %s [options] <subcommand> <args...>\n\
\n\
Where subcommand is:\n\
    prepare   -- prepares the disk for integrity protection\n\
    protect   -- adds verity partitions and signs cvmboot.cpio\n\
    init      -- peforms both prepare and protect operations\n\
    state     -- print the state of disk image (base, prepared, protected)\n\
    shell     -- shell into a disk image\n\
\n\
Options:\n\
    --help    -- print this help message\n\
    --verbose -- print additional output\n\
    --trace   -- print tracing output\n\
\n\
Examples:\n\
    $ sudo cvmdisk prepare <input-disk> <output-disk>\n\
    $ sudo cvmdisk protect <disk> <signing-tool>\n\
    $ sudo cvmdisk init <input-disk> <output-disk> <signing-tool>\n\
    $ sudo cvmdisk shell <disk>\n\
\n";

int main(int argc, const char* argv[])
{
    err_t err = ERR_INITIALIZER;

    err_set_arg0(argv[0]);

    /* check arguments */
    if (argc < 2)
    {
        printf(USAGE, argv[0]);
        exit(1);
    }

    /* Check for dependent programs */
    _check_program("azcopy");
    _check_program("blkid");
    _check_program("blockdev");
    _check_program("cat");
    _check_program("chmod");
    _check_program("chown");
    _check_program("chroot");
    _check_program("cp");
    _check_program("dd");
    _check_program("dumpe2fs");
    _check_program("dmsetup");
    _check_program("e2fsck");
    _check_program("echo");
    _check_program("find");
    _check_program("fusermount");
    _check_program("gunzip");
    _check_program("losetup");
    _check_program("mkdir");
    _check_program("mv");
    _check_program("objcopy");
    _check_program("openssl");
    _check_program("resize2fs");
    _check_program("rm");
    _check_program("sed");
    _check_program("sgdisk");
    _check_program("sparsefs-mount");

    /* Prepend "/boot/efi" to all EFI paths */
    paths_set_prefix("/boot/efi");

    /* determine location of sharedir */
    if (locate_sharedir(argv[0]) != 0)
        ERR("failed to determine location of shared directory");

    if (getoption(&argc, argv, "--help", NULL, &err) == 0)
        g_options.help = true;
    else if (getoption(&argc, argv, "-h", NULL, &err) == 0)
        g_options.help = true;

    if (getoption(&argc, argv, "--version", NULL, &err) == 0)
        g_options.version = true;
    else if (getoption(&argc, argv, "-v", NULL, &err) == 0)
        g_options.version = true;

    if (getoption(&argc, argv, "--verbose", NULL, &err) == 0)
        g_options.verbose = true;
    else if (getoption(&argc, argv, "-v", NULL, &err) == 0)
        g_options.verbose = true;

    if (getoption(&argc, argv, "--trace", NULL, &err) == 0)
    {
        g_options.trace = true;
        execf_set_trace(true);
    }
    else if (getoption(&argc, argv, "-t", NULL, &err) == 0)
        g_options.trace = true;

    if (getoption(&argc, argv, "--etrace", NULL, &err) == 0)
    {
        g_options.etrace = true;
        err_show_file_line_func(true);
    }

    if (g_options.help && argc == 1)
    {
        printf(USAGE, argv[0]);
        exit(1);
    }

    if (g_options.version && argc == 1)
    {
        printf("%s\n", CVMBOOT_VERSION);
        exit(0);
    }

    const char* subcommand = argv[1];

    if (strcmp(subcommand, "prepare") == 0)
    {
        user_opt_t user;
        hostname_opt_t hostname;
        bool skip_resolv_conf = false;
        bool use_resource_disk = false;
        bool use_thin_provisioning = true;
        bool verify = false;
        const char* events = NULL;
        const char* opt;
        bool expand_root_partition = false;
        bool no_strip = false;

        memset(&user, 0, sizeof(user));
        memset(&hostname, 0, sizeof(hostname));
        _check_root();

        if (getoption(&argc, argv, "--no-strip", NULL, &err) == 0)
            no_strip = true;

        if (getoption(&argc, argv, "--skip-resolv-conf", NULL, &err) == 0)
            skip_resolv_conf = true;

        if (getoption(&argc, argv, "--use-resource-disk", NULL, &err) == 0)
            use_resource_disk = true;

        if (getoption(&argc, argv, "--no-thin-provisioning", NULL, &err) == 0)
            use_thin_provisioning = false;

        if (getoption(&argc, argv, "--verify", NULL, &err) == 0)
            verify = true;

        if (getoption(&argc, argv, "--expand-root-partition", NULL, &err) == 0)
            expand_root_partition = true;

        /* get the --user option */
        _get_user_option(&argc, argv, &user);

        if (getoption(&argc, argv, "--events", &opt, &err) == 0)
        {
            if (access(opt, R_OK) != 0)
                ERR("file does not exist: --events=%s", opt);

            events = opt;
        }

        if (getoption(&argc, argv, "--hostname", &opt, &err) == 0)
        {
            if (!_is_valid_hostname(opt))
                ERR("--hostname option argument is invalid: %s", opt);

            strlcpy(hostname.buf, opt, sizeof(hostname.buf));
        }

        return _subcommand_prepare(argc, argv, &user, &hostname, events,
            skip_resolv_conf, use_resource_disk, use_thin_provisioning,
            verify, expand_root_partition, no_strip);
    }
    else if (strcmp(subcommand, "protect") == 0)
    {
        bool verify = false;

        _check_root();

        if (getoption(&argc, argv, "--verify", NULL, &err) == 0)
            verify = true;

        /* Handle old-style private.pem/public.pem parameters */
        if (argc == 5)
        {
            if (_create_cvmsign_public_private_keys(argv[3], argv[4]) == 0)
            {
                argv[3] = "cvmsign";
                argv[4] = NULL;
                argc--;
            }
        }

        return _subcommand_protect(argc, argv, verify);
    }
    else if (strcmp(subcommand, "init") == 0)
    {
        user_opt_t user;
        hostname_opt_t hostname;
        const char* events = NULL;
        const char* opt;
        bool delta = false;
        bool skip_resolv_conf = false;
        bool use_resource_disk = false;
        bool use_thin_provisioning = true;
        bool verify = false;
        bool expand_root_partition = false;
        bool no_strip = false;

        memset(&user, 0, sizeof(user));
        memset(&hostname, 0, sizeof(hostname));
        _check_root();

        if (getoption(&argc, argv, "--delta", NULL, &err) == 0)
            delta = true;

        if (getoption(&argc, argv, "--skip-resolv-conf", NULL, &err) == 0)
            skip_resolv_conf = true;

        if (getoption(&argc, argv, "--use-resource-disk", NULL, &err) == 0)
            use_resource_disk = true;

        if (getoption(&argc, argv, "--no-thin-provisioning", NULL, &err) == 0)
            use_thin_provisioning = false;

        if (getoption(&argc, argv, "--verify", NULL, &err) == 0)
            verify = true;

        if (getoption(&argc, argv, "--no-strip", NULL, &err) == 0)
            no_strip = true;

        if (getoption(&argc, argv, "--expand-root-partition", NULL, &err) == 0)
            expand_root_partition = true;

        /* get the --user option */
        _get_user_option(&argc, argv, &user);

        if (getoption(&argc, argv, "--events", &opt, &err) == 0)
        {
            if (access(opt, R_OK) != 0)
                ERR("file does not exist: --events=%s", opt);

            events = opt;
        }

        if (getoption(&argc, argv, "--hostname", &opt, &err) == 0)
        {
            if (!_is_valid_hostname(opt))
                ERR("--hostname option argument is invalid: %s", opt);

            strlcpy(hostname.buf, opt, sizeof(hostname.buf));
        }

        /* Handle old-style private.pem/public.pem parameters */
        if (argc == 6)
        {
            if (_create_cvmsign_public_private_keys(argv[4], argv[5]) == 0)
            {
                argv[4] = "cvmsign";
                argv[5] = NULL;
                argc--;
            }
        }

        return _subcommand_init(argc, argv, &user, &hostname, events, delta,
            skip_resolv_conf, use_resource_disk, use_thin_provisioning,
            verify, expand_root_partition, no_strip);
    }
    else if (strcmp(subcommand, "state") == 0)
    {
        _check_root();
        return _subcommand_state(argc, argv);
    }
    else if (strcmp(subcommand, "shell") == 0)
    {
        bool read_only = false;
        bool nobind = false;

        _check_root();

        if (getoption(&argc, argv, "--read-only", NULL, &err) == 0)
            read_only = true;

        if (getoption(&argc, argv, "--ro", NULL, &err) == 0)
            read_only = true;

        if (getoption(&argc, argv, "--nobind", NULL, &err) == 0)
            nobind = true;

        return _subcommand_shell(argc, argv, read_only, nobind);
    }
    else if (strcmp(subcommand, "expand-root-partition") == 0)
    {
        _check_root();
        return _subcommand_expand_root_partition(argc, argv);
    }
    else if (strcmp(subcommand, "losetup") == 0)
    {
        _check_root();
        if (argc >= 3)
        {
            gpt_t* gpt;

            losetup(argv[2], globals.loop);

            if (gpt_open(globals.loop, O_RDWR | O_EXCL, &gpt) < 0)
                ERR("failed to open %s", globals.loop);

            if (gpt_sync(gpt) < 0)
                ERR("gpt_sync() failed");

            gpt_close(gpt);

            printf("sudo losetup -d %s\n", globals.loop);
        }
        return 0;
    }
    else if (strcmp(subcommand, "genkeys") == 0)
    {
        return _subcommand_genkeys(argc, argv);
    }
    else if (strcmp(subcommand, "gpt") == 0)
    {
        _check_root();
        return _subcommand_gpt(argc, argv);
    }
    else if (strcmp(subcommand, "fixgpt") == 0)
    {
        _check_root();
        _setup_loopback(argc, argv);

        return _subcommand_fixgpt(argc, argv);
    }
    else if (strcmp(subcommand, "digest") == 0)
    {
        _subcommand_digest(argc, argv);
    }
    else if (strcmp(subcommand, "azcopy") == 0)
    {
        _check_program("azcopy");
        _subcommand_azcopy(argc, argv);
    }
    else if (strcmp(subcommand, "copy") == 0)
    {
        _subcommand_copy(argc, argv);
    }
    else
    {
        printf("%s: unknown subcommand: %s\n", argv[0], subcommand);
        exit(1);
    }

    return 0;
}
