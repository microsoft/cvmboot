// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <common/cvmvhd.h>

const char* g_arg0;
const char* g_arg1;

__attribute__((format(printf, 1, 2)))
static void _err(const char* fmt, ...)
{
    va_list ap;

    if (g_arg1)
        fprintf(stderr, "%s %s: error: ", g_arg0, g_arg1);
    else
        fprintf(stderr, "%s: error: ", g_arg0);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(1);
}

static int _str2u64(const char* s, uint64_t* result)
{
    char* end = NULL;
    uint64_t x;

    x = strtoul(s, &end, 10);

    if (!end || *end)
        return -1;

    *result = x;

    return 0;
}

static int _subcommand_dump(int argc, const char* argv[])
{
    cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;
    const char* vhd_file;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s %s <vhd-file>\n", g_arg0, g_arg1);
        exit(1);
    }

    vhd_file = argv[2];

    if (cvmvhd_dump(vhd_file, &err) < 0)
        _err("%s", err.buf);

    return 0;
}

static int _subcommand_resize(int argc, const char* argv[])
{
    uint64_t size_gb = 0;
    const size_t gb = 1024*1024*1024;
    cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

    /* Check usage */
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s %s <vhd-file> <num-gigabytes>]\n",
            g_arg0, g_arg1);
        exit(1);
    }

    const char* vhd_file= argv[2];
    const char* num_gigabytes = argv[3];

    if (_str2u64(num_gigabytes, &size_gb) < 0)
        _err("invalid size argument: '%s'\n", num_gigabytes);

    if (size_gb == 0)
        _err("size argument must be non-zero: '%s'\n", num_gigabytes);

    if (cvmvhd_resize(vhd_file, size_gb*gb, &err) < 0)
        _err("%s", err.buf);

    return 0;
}

static int _subcommand_create(int argc, const char* argv[])
{
    size_t size_gb;
    cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

    /* Check usage */
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s %s <vhd-file> <num-gigabytes>\n",
            g_arg0, g_arg1);
        exit(1);
    }

    const char* vhd_file = argv[2];
    const char* num_gigabytes = argv[3];

    /* Extract the gigabyte size argument */
    if (_str2u64(num_gigabytes, &size_gb) < 0)
        _err("invalid size argument: '%s'\n", num_gigabytes);

    if (size_gb == 0)
        _err("size argument must be non-zero: '%s'\n", num_gigabytes);

    if (cvmvhd_create(vhd_file, size_gb, &err) < 0)
        _err("%s", err.buf);

    return 0;
}

static int _subcommand_append(int argc, const char* argv[])
{
    cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

    /* Check usage */
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s %s <vhd-file>\n", g_arg0, g_arg1);
        exit(1);
    }

    const char* vhd_file = argv[2];

    if (cvmvhd_append(vhd_file, &err) < 0)
        _err("%s", err.buf);

    return 0;
}

static int _subcommand_remove(int argc, const char* argv[])
{
    cvmvhd_error_t err = CVMVHD_ERROR_INITIALIZER;

    /* Check usage */
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s %s <vhd-file>\n", g_arg0, g_arg1);
        exit(1);
    }

    const char* vhd_file = argv[2];

    if (cvmvhd_remove(vhd_file, &err) < 0)
        _err("%s", err.buf);

    return 0;
}

#define USAGE \
"Usage: %s subcommand arguments...\n" \
"\n" \
"Subcommands:\n" \
"    create <vhd-file> <num-gigabytes> -- create new VHD file\n" \
"    resize <vhd-file> <percentage>|<num-gigabytes> -- resize VHD file\n" \
"    append <vhd-file> -- append VHD trailer to file (or replace)\n" \
"    remove <vhd-file> -- remove VHD trailer from VHD file (if any)\n" \
"    dump <vhd-file> -- dump VHD trailer\n" \
"\n"

int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, USAGE, argv[0]);
        exit(1);
    }

    g_arg0 = argv[0];
    g_arg1 = argv[1];

    if (strcmp(argv[1], "dump") == 0)
    {
        return _subcommand_dump(argc, argv);
    }
    else if (strcmp(argv[1], "resize") == 0)
    {
        return _subcommand_resize(argc, argv);
    }
    else if (strcmp(argv[1], "create") == 0)
    {
        return _subcommand_create(argc, argv);
    }
    else if (strcmp(argv[1], "append") == 0)
    {
        return _subcommand_append(argc, argv);
    }
    else if (strcmp(argv[1], "remove") == 0)
    {
        return _subcommand_remove(argc, argv);
    }
    else
    {
        _err("unknown subcommand: %s", argv[1]);
    }

    return 0;
}
