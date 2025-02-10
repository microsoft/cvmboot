// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define _GNU_SOURCE
#define FUSE_USE_VERSION 31
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <fuse.h>
#include <sys/mman.h>
#include <openssl/sha.h>
#include <common/strings.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define TYPEFLAG_LNKTYPE '1' /* indicates node is a hard link */

#define BLOCK_SIZE HASH_BLOCK_SIZE

#define ENABLE_HASH_CHECKS

// reference: /usr/include/fuse/fuse.h

static const char* arg0;
static char basedir[PATH_MAX];

typedef struct options
{
    int help; /* print help/usage message if non-zero */
    int trace; /* enable tracing if non-zero */
    int foreground; /* run in the foreground if non-zero */
}
options_t;

static options_t _options;

#define FILE_HANDLE_MAGIC 0xd32dc6db1ddd4622

typedef struct _file_handle
{
    uint64_t magic;
    int fd;
    size_t peak; /* peak zero-write */
}
file_handle_t;

static const struct fuse_opt _fuse_opts[] =
{
    {"-h", offsetof(options_t, help), 1},
    {"--help", offsetof(options_t, help), 1},
    {"-t", offsetof(options_t, trace), 1},
    {"--trace", offsetof(options_t, trace), 1},
    {"-f", offsetof(options_t, foreground), 1},
    FUSE_OPT_END
};

static inline size_t _max(size_t x, size_t y)
{
    return (x > y) ? x : y;
}

static inline int _punch_hole(int fd, size_t offset, size_t len)
{
    const int mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;

    if (fallocate(fd, mode, offset, len) < 0)
        return -errno;

    return len;
}

__attribute__((format(printf, 1, 2)))
static inline void _trace(const char* format, ...)
{
    if (_options.trace)
    {
        va_list ap;
        va_start(ap, format);
        vfprintf(stdout, format, ap);
        va_end(ap);
    }
}

static ssize_t _readn(int fd, void* data, size_t size, off_t off)
{
    ssize_t ret = 0;
    unsigned char* p = (unsigned char*)data;
    size_t r = size;
    ssize_t nread = 0;

    while (r)
    {
        ssize_t n = pread(fd, p, r, off);

        if (n > 0)
        {
            p += n;
            r -= n;
            off += n;
            nread += n;
        }
        else if (n == 0)
        {
            ret = nread;
            goto done;
        }
        else
        {
            ret = -errno;
            goto done;
        }
    }

    ret = nread;

done:
    return ret;
}

static ssize_t _writen(int fd, const void* data, size_t size, off_t off)
{
    ssize_t ret = 0;
    const unsigned char* p = (const unsigned char*)data;
    size_t r = size;

    while (r)
    {
        ssize_t n = pwrite(fd, p, r, off);

        if (n > 0)
        {
            p += n;
            r -= n;
            off += n;
        }
        else if (n == 0)
        {
            ret = -EIO;
            goto done;
        }
        else
        {
            ret = -errno;
            goto done;
        }

    }

    ret = size;

done:
    return ret;
}

static void* _fs_init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
    cfg->kernel_cache = 1;

    return NULL;
}

static int _fs_getattr(
    const char* path,
    struct stat* statbuf,
    struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    char fullpath[3*PATH_MAX];

    _trace("%s(): path=%s\n", func, path);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if ((ret = stat(fullpath, statbuf)) < 0)
    {
        ret = -errno;
        goto done;
    }

done:
    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_opendir(const char* name, struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    DIR* dir;
    char fullpath[3*PATH_MAX];

    _trace("%s(): name=%s\n", func, name);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, name);

    if ((dir = opendir(fullpath)) == NULL)
    {
        ret = -errno;
        goto done;
    }

    _trace("%s(): dir=%p\n", func, dir);

    fi->fh = (uint64_t)dir;

done:
    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

/* ATTN: pass non-zero offsets to filler function to prevent overflow */
static int _fs_readdir(
    const char* path,
    void* buf,
    fuse_fill_dir_t filler,
    off_t offset,
    struct fuse_file_info* fi,
    enum fuse_readdir_flags flags)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    struct dirent* ent;
    DIR* dir = (DIR*)fi->fh;

    _trace("%s(): path=%s offset=%lu\n", func, path, offset);

    _trace("%s(): dir=%p\n", func, dir);

    errno = 0;

    while ((ent = readdir(dir)) != NULL)
    {
        _trace("%s(): name=%s\n", func, ent->d_name);
        filler(buf, ent->d_name, NULL, 0, 0);
    }

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_releasedir(const char* name, struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    DIR* dir = (DIR*)fi->fh;

    _trace("%s(): name=%s\n", func, name);

    if (closedir(dir) < 0)
    {
        ret = -errno;
        goto done;
    }

done:
    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_create(const char * path, mode_t mode, struct fuse_file_info* fi)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    char fullpath[3*PATH_MAX];
    int fd = -1;
    file_handle_t* fh;

    _trace("%s(): path=%s mode=%u\n", func, path, mode);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if ((fd = creat(fullpath, mode)) < 0)
    {
        ret = -errno;
        goto done;
    }

    if (!(fh = malloc(sizeof(file_handle_t))))
    {
        ret = -ENOMEM;
        goto done;
    }

    fh->magic = FILE_HANDLE_MAGIC;
    fh->fd = fd;
    fd = -1;
    fh->peak = 0;
    fi->fh = (uint64_t)fh;

done:

    if (fd >= 0)
        close(fd);

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_open(const char* path, struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    int fd = -1;
    char fullpath[3*PATH_MAX];
    file_handle_t* fh;

    _trace("%s(): path=%s\n", func, path);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if ((fd = open(fullpath, fi->flags)) < 0)
    {
        ret = -errno;
        goto done;
    }

    if (!(fh = malloc(sizeof(file_handle_t))))
    {
        ret = -ENOMEM;
        goto done;
    }

    fh->magic = FILE_HANDLE_MAGIC;
    fh->fd = fd;
    fd = -1;
    fh->peak = 0;
    fi->fh = (uint64_t)fh;

done:

    if (fd >= 0)
        close(fd);

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

/* called when final reference to an open file is closed */
static int _fs_release(const char* path, struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    file_handle_t* fh = (file_handle_t*)fi->fh;

    _trace("%s(): path=%s\n", func, path);

    /* ATTN: extend file to maxsize */
    close(fh->fd);

    _trace("%s(): ret=%d\n", func, ret);

    return ret;
}

static int _fs_read(
    const char* path,
    char* buf,
    size_t size,
    off_t offset,
    struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    file_handle_t* fh = (file_handle_t*)fi->fh;

    _trace("%s(): path=%s size=%zu offset=%lu\n", func, path, size, offset);

    ret = (int)_readn(fh->fd, buf, size, offset);

    _trace("%s(): ret=%d\n", func, ret);

    return ret;
}

static int _fs_write(
    const char* path,
    const char* buf,
    size_t size,
    off_t offset,
    struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    file_handle_t* fh = (file_handle_t*)fi->fh;
    ssize_t r;
    static const size_t blksz = 128 * 1024;
    size_t off = offset;
    const uint8_t* ptr = (const uint8_t*)buf;
    size_t nblocks = size / blksz;
    size_t rem = size % blksz;
    struct stat statbuf;

    _trace("%s(): path=%s size=%zu offset=%lu\n", func, path, size, offset);

    /* Write whole blocks */
    for (size_t i = 0; i < nblocks; i++)
    {
        if (all_zeros(ptr, blksz))
        {
            bool extend = false;

            if (fstat(fh->fd, &statbuf) < 0)
            {
                ret = -errno;
                goto done;
            }

            /* If file is being extended */
            if (off + blksz > statbuf.st_size)
            {
                extend = true;
            }

            if (off < statbuf.st_size)
            {
                if ((r = _punch_hole(fh->fd, off, blksz)) < 0)
                {
                    ret = r;
                    goto done;
                }
            }

            if (extend)
            {
                if (ftruncate(fh->fd, off + blksz) < 0)
                {
                    ret = -errno;
                    goto done;
                }
            }
        }
        else if ((r = _writen(fh->fd, ptr, blksz, off)) < 0)
        {
            ret = r;
            goto done;
        }

        if (r != blksz)
        {
            ret = -EIO;
            goto done;
        }

        off += r;
        ptr += r;
    }

    /* Write remaining bytes */
    if (rem > 0)
    {
        if (all_zeros(ptr, rem))
        {
            bool extend = false;

            if (fstat(fh->fd, &statbuf) < 0)
            {
                ret = -errno;
                goto done;
            }

            /* If file is being extended */
            if (off + rem > statbuf.st_size)
            {
                extend = true;
            }

            if (off < statbuf.st_size)
            {
                if ((r = _punch_hole(fh->fd, off, rem)) < 0)
                {
                    ret = r;
                    goto done;
                }
            }

            if (extend)
            {
                if (ftruncate(fh->fd, off + rem) < 0)
                {
                    ret = -errno;
                    goto done;
                }
            }
        }
        else if ((r = _writen(fh->fd, ptr, rem, off)) < 0)
        {
            ret = r;
            goto done;
        }

        if (r != rem)
        {
            ret = -EIO;
            goto done;
        }

        off += r;
        ptr += r;
    }

    ret = (int)(off - offset);

done:
    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static off_t _fs_lseek(const char* path, off_t off, int whence, struct fuse_file_info* fi)
{
    const char* func = __FUNCTION__;
    int ret = 0;
    file_handle_t* fh = (file_handle_t*)fi->fh;

    _trace("%s(): path=%s off=%zd whence=%d\n", func, path, off, whence);

    if ((ret = lseek(fh->fd, off, whence)) < 0)
    {
        ret = -errno;
        goto done;
    }

done:
    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_readlink(const char* path, char* buf, size_t bufsize)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    char fullpath[3*PATH_MAX];

    _trace("%s(): path=%s buf=%p bufsize=%zu\n", func, path, buf, bufsize);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if ((ret = readlink(fullpath, buf, bufsize)) < 0)
        ret = -errno;

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static void _fs_destroy(void* private_data)
{
    const char* func = __FUNCTION__;

    _trace("%s(): private_data=%p\n", func, private_data);
    _trace("%s(): ret=%d\n", func, 0);
}

static int _fs_utimens(
    const char* path,
    const struct timespec tv[2],
    struct fuse_file_info* fi)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    char fullpath[3*PATH_MAX];
    int fd = -1;

    _trace("%s(): path=%s tv=%p fi=%p\n", func, path, tv, fi);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if ((fd = open(fullpath, O_RDONLY)) < 0)
    {
        ret = -errno;
        goto done;
    }

    if (futimens(fd, tv) < 0)
    {
        ret = -errno;
        goto done;
    }

done:

    if (fd >= 0)
        close(fd);

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_unlink(const char* path)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    char fullpath[3*PATH_MAX];

    _trace("%s(): path=%s\n", func, path);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if (unlink(fullpath))
    {
        ret = -errno;
        goto done;
    }

done:

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_truncate(const char* path, off_t offset, struct fuse_file_info* fi)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    char fullpath[3*PATH_MAX];

    _trace("%s(): path=%s\n", func, path);

    snprintf(fullpath, sizeof(fullpath), "%s/%s", basedir, path);

    if (truncate(path, offset) < 0)
    {
        //ret = -errno;
        goto done;
    }

done:

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static int _fs_rename(const char* oldpath, const char* newpath, unsigned int flags)
{
    int ret = 0;
    const char* func = __FUNCTION__;
    char fulloldpath[3*PATH_MAX];
    char fullnewpath[3*PATH_MAX];

    _trace("%s(): oldpath=%s newpath=%s\n", func, oldpath, newpath);

    snprintf(fulloldpath, sizeof(fulloldpath), "%s/%s", basedir, oldpath);
    snprintf(fullnewpath, sizeof(fullnewpath), "%s/%s", basedir, newpath);

    if (rename(fulloldpath, fullnewpath) < 0)
    {
        ret = -errno;
        goto done;
    }

done:

    _trace("%s(): ret=%d\n", func, ret);
    return ret;
}

static const struct fuse_operations _operations =
{
    .init = _fs_init,
    .getattr = _fs_getattr,
    .opendir = _fs_opendir,
    .readdir = _fs_readdir,
    .releasedir = _fs_releasedir,
    .open = _fs_open,
    .create = _fs_create,
    .release = _fs_release,
    .read = _fs_read,
    .write = _fs_write,
    .readlink = _fs_readlink,
    .destroy = _fs_destroy,
    .utimens = _fs_utimens,
    .unlink = _fs_unlink,
    .truncate = _fs_truncate,
    .rename = _fs_rename,
    .lseek = _fs_lseek,
};

#define USAGE                                                             \
    "Usage: %s [options] <basedir> <mountpoint>\n" \
    "\n"                                                                  \
    "File-system specific options:\n"                                     \
    "    -t  --trace            enable file-system specific tracing\n"    \
    "\n"

__attribute__((__unused__))
static void _dump_args(int argc, const char* argv[])
{
    for (int i = 0; i < argc; i++)
        printf("argv[%s]\n", argv[i]);

    printf("\n");
}

/* remove the i-th argument from argv and update argc */
static void _remove_arg(int* argc, const char* argv[], int i)
{
    size_t n = *argc - i + 1;
    memmove(&argv[i - 1], &argv[i], sizeof(char*) * n);
    (*argc)--;
}

int main(int argc, const char* argv[])
{
    int ret = 1;
    struct fuse_args args = FUSE_ARGS_INIT(argc, (char**)argv);

    arg0 = argv[0];

    /* Parse options */
    if (fuse_opt_parse(&args, &_options, _fuse_opts, NULL) == -1)
        goto done;

    /* check the command line arguments and check for --help option */
    if (args.argc != 3 || _options.help)
    {
        /* print the file-system specific usage message */
        printf(USAGE, arg0);

        /* add --help to args, to make FUSE print its help below */
        fuse_opt_add_arg(&args, "--help");

        /* make program name the empty string */
        args.argv[0][0] = '\0';
    }
    else
    {
        /* capture the file-system specific arguments */
        if (realpath(args.argv[1], basedir) == NULL)
        {
            fprintf(stderr, "%s: directory not found: %s\n",
                arg0, args.argv[1]);
            exit(1);
        }

        /* remove file-system specific args so fuse won't see them */
        _remove_arg(&args.argc, (const char**)args.argv, 1);

        if (_options.foreground)
            fuse_opt_add_arg(&args, "-f");
    }

    /* run the fuse_main() program (may print help if --help added above) */
    ret = fuse_main(args.argc, args.argv, &_operations, NULL);

    /* release arguments */
    fuse_opt_free_args(&args);

    ret = 0;

done:
    return ret;
}
