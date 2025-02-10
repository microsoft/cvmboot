// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "cpio.h"
#include <string.h>
#include <stdlib.h>
#include "allocator.h"

typedef struct _CPIOHeader
{
   char magic[6];
   char ino[8];
   char mode[8];
   char uid[8];
   char gid[8];
   char nlink[8];
   char mtime[8];
   char filesize[8];
   char devmajor[8];
   char devminor[8];
   char rdevmajor[8];
   char rdevminor[8];
   char namesize[8];
   char check[8];
}
CPIOHeader;

static int _HexToInt(const char* str, unsigned int len)
{
    const char* p;
    int r = 1;
    int x = 0;

    for (p = str + len; p != str; p--)
    {
        int xdigit = p[-1];
        int d;

        if (xdigit >= '0' && xdigit <= '9')
        {
            d = xdigit - '0';
        }
        else if (xdigit >= 'A' && xdigit <= 'F')
        {
            d = (xdigit - 'A') + 10;
        }
        else
            return -1;

        x += r * d;
        r *= 16;
    }

    return x;
}

static int _RoundUpToMultiple(size_t x, size_t m)
{
    return (int)((x + (m - 1)) / m * m);
}

static int _MatchMagicNumber(const char* s)
{
    return
        s[0] == '0' && s[1] == '7' && s[2] == '0' &&
        s[3] == '7' && s[4] == '0' && s[5] == '1';
}

static int _GetFileSize(const CPIOHeader* header)
{
    return _HexToInt(header->filesize, 8);
}

static int _GetNameSize(const CPIOHeader* header)
{
    return _HexToInt(header->namesize, 8);
}

static const char* _GetName(const CPIOHeader* header)
{
    return (char*)(header + 1);
}

/* Get total size of an entry: HEADER + NAME + DATA + PADDING */
static int _GetEntrySize(const CPIOHeader* header)
{
    int filesize;
    int namesize;

    if ((filesize = _GetFileSize(header)) == -1)
        return -1;

    if ((namesize = _GetNameSize(header)) == -1)
        return -1;

    return
        _RoundUpToMultiple(sizeof(CPIOHeader) + namesize, 4) +
        _RoundUpToMultiple(filesize, 4);
}

static int _CheckEntry(
    const CPIOHeader* header,
    const void* cpioEnd)
{
    int rc = 0;
    int remaining;
    int size;

    /* Calculate the remaining space */
    remaining = (int)((char*)cpioEnd - (char*)header);

    /* If not enough space left for another header */
    if (sizeof(CPIOHeader) > remaining)
    {
        rc = -1;
        goto done;
    }

    /* Check magic number */
    if (!_MatchMagicNumber(header->magic))
    {
        rc = -1;
        goto done;
    }

    /* Get total size of this entry */
    if ((size = _GetEntrySize(header)) == -1)
    {
        rc = -1;
        goto done;
    }

    /* If not enough space left */
    if (size > remaining)
    {
        rc = -1;
        goto done;
    }

done:
    return rc;
}

/* Get total size of an entry: HEADER + NAME + DATA + PADDING */
static int _NextHeader(
    const CPIOHeader** header,
    const void* cpioEnd)
{
    int rc = 0;
    int size;
    const char* name;

    /* Check parameters */
    if (!header || !*header)
    {
        rc = -1;
        goto done;
    }

    /* Check for valid entry */
    if (_CheckEntry(*header, cpioEnd) != 0)
    {
        *header = NULL;
        rc = -1;
        goto done;
    }

    /* Get the name */
    name = (const char*)(*header + 1);

    /* If this is the trailer, then no more headers */
    if (strcmp(name, "TRAILER!!!") == 0)
    {
        *header = NULL;
        rc = 0;
        goto done;
    }

    /* Get total size of this entry */
    if ((size = _GetEntrySize(*header)) == -1)
    {
        rc = -1;
        goto done;
    }

    /* Get the next header */
    *header = (CPIOHeader*)((char*)(*header) + size);

    /* Check for valid entry */
    if (_CheckEntry(*header, cpioEnd) != 0)
    {
        *header = NULL;
        rc = -1;
        goto done;
    }

done:
    return rc;
}

static int _FindHeader(
    const void* cpio_data,
    size_t cpio_size,
    const char* path,
    const CPIOHeader** headerOut)
{
    int rc = -1;
    const CPIOHeader* header = (const CPIOHeader*)cpio_data;
    const void* cpioEnd = (char*)cpio_data + cpio_size;

    *headerOut = NULL;

    while (header)
    {
        if (_CheckEntry(header, cpioEnd) != 0)
            goto done;

        if (strcmp(_GetName(header), path) == 0)
        {
            *headerOut = header;
            rc = 0;
            goto done;
        }

        if (_NextHeader(&header, cpioEnd) != 0)
            goto done;
    }

done:
    return rc;
}

int cpio_get_file_direct(
    const void* cpio_data,
    size_t cpio_size,
    const char* path,
    const void** data_out,
    size_t* size_out)
{
    int rc = -1;
    const CPIOHeader* header = NULL;
    size_t headerSize;
    void* fileData = NULL;
    size_t fileSize;

    /* Check parameters */
    if (!cpio_data || !cpio_size || !path || !data_out || !size_out)
        goto done;

    if (cpio_size < sizeof(CPIOHeader))
        goto done;

    if (!_MatchMagicNumber(((const CPIOHeader*)cpio_data)->magic))
        goto done;

    /* Initialize output parameters */
    *data_out = NULL;
    *size_out = 0;

    /* Find the CPIO header for this path */
    if (_FindHeader(cpio_data, cpio_size, path, &header) != 0)
        goto done;

    /* Determine size of this file */
    fileSize = _GetFileSize(header);

    /* Compute the full full size of header (including path) */
    headerSize = sizeof(CPIOHeader);
    headerSize += _GetNameSize(header);
    headerSize = _RoundUpToMultiple(headerSize, 4);

    /* Set pointer to data */
    fileData = (unsigned char*)header + headerSize;

    /* Set output parameters */
    *data_out = fileData;
    *size_out = fileSize;

    rc = 0;

done:

    return rc;
}

int cpio_get_file(
    const void* cpio_data,
    size_t cpio_size,
    const char* path,
    void** data_out,
    size_t* size_out)
{
    int rc = -1;
    const void* fileData = NULL;
    size_t fileSize;
    void* data = NULL;
    size_t size;

    /* Check parameters */
    if (!cpio_data || !cpio_size || !path || !data_out || !size_out)
        goto done;

    /* Get a direct pointer to the file */
    if (cpio_get_file_direct(
        cpio_data,
        cpio_size,
        path,
        &fileData,
        &fileSize) < 0)
    {
        goto done;
    }

    size = fileSize;

    /* Allocate the memory for this file (extra byte for zero terminator) */
    if (!(data = __allocator.alloc(size + 1)))
        goto done;

    memcpy(data, fileData, size);
    ((char*)data)[size] = '\0';

    /* Set output parameters */
    *data_out = data;
    data = NULL;
    *size_out = size;

    rc = 0;

done:

    if (data)
        __allocator.free(data);

    return rc;
}
