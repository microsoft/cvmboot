// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <utils/hexstr.h>
#include "guid.h"
#include "random.h"

/* 21686148-6449-6e6f-744e-656564454649 -- MBR partition type */
const guid_t mbr_type_guid =
{
    0x21686148,
    0x6449,
    0x6e6f,
    { 0x74, 0x4e, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49 }
};

/* c12a7328-f81f-11d2-ba4b-00a0c93ec93b -- EFI partition type */
const guid_t efi_type_guid =
{
    0xc12a7328,
    0xf81f,
    0x11d2,
    { 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b }
};

/* "0fc63daf-8483-4772-8e79-3d69d8477de4" -- Linux partition type */
const guid_t linux_type_guid =
{
    0x0fc63daf,
    0x8483,
    0x4772,
    { 0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 },
};

/* "c148c601-508c-4f28-aa23-3c1a6955f649" -- Upper-layer partition type */
const guid_t rootfs_upper_type_guid =
{
    0xc148c601,
    0x508c,
    0x4f28,
    { 0xaa, 0x23, 0x3c, 0x1a, 0x69, 0x55, 0xf6, 0x49 }
};

/* d00e1e63-97b6-499c-9d2f-d76b8356450f -- EFI upper-layer partition type */
const guid_t efi_upper_type_guid =
{
    0xd00e1e63,
    0x97b6,
    0x499c,
    { 0x9d, 0x2f, 0xd7, 0x6b, 0x83, 0x56, 0x45, 0x0f }
};

/* "136ce4af-afed-4f96-84ff-0651088074ee" -- thin-data partition type */
const guid_t thin_data_type_guid =
{
    0x136ce4af,
    0xafed,
    0x4f96,
    { 0x84, 0xff, 0x06, 0x51, 0x08, 0x80, 0x74, 0xee, }
};

/* "ed71d74e-250a-4f9f-a29b-32246f9bb43a" -- thin-meta partition type */
const guid_t thin_meta_type_guid =
{
    0xed71d74e,
    0x250a,
    0x4f9f,
    { 0xa2, 0x9b, 0x32, 0x24, 0x6f, 0x9b, 0xb4, 0x3a, }
};

/* "3416e185-0efa-4ba5-bf43-be206e7f9af0" -- verity partition type */
const guid_t verity_type_guid =
{
    0x3416e185,
    0x0efa,
    0x4ba5,
    { 0xbf, 0x43, 0xbe, 0x20, 0x6e, 0x7f, 0x9a, 0xf0, }
};

int guid_generate(guid_t* guid)
{
    uint8_t bytes[16];
    get_random_bytes(bytes, sizeof(bytes));
    return guid_init_bytes(guid, bytes);
}

int guid_init_xy(guid_t* guid, uint64_t x, uint64_t y)
{
    if (!guid)
        return -1;

    /* 84B9702C-702C-702C-A6A6-A6A6A6A6A6A6 */
    guid->data1 = x & 0x00000000FFFFFFFF;
    guid->data2 = (x & 0x0000FFFF00000000) >> 32;
    guid->data3 = (x & 0xFFFF000000000000) >> 48;
    guid->data4[0] = y & 0x00000000000000FF;
    guid->data4[1] = (y & 0x000000000000FF00) >> 8;
    guid->data4[2] = (y & 0x0000000000FF0000) >> 16;
    guid->data4[3] = (y & 0x00000000FF000000) >> 24;
    guid->data4[4] = (y & 0x000000FF00000000) >> 32;
    guid->data4[5] = (y & 0x0000FF0000000000) >> 40;
    guid->data4[6] = (y & 0x00FF000000000000) >> 48;
    guid->data4[7] = (y & 0xFF00000000000000) >> 56;

    return 0;
}

int guid_init_bytes(guid_t* guid, const uint8_t bytes[GUID_BYTES])
{
    if (!guid || !bytes)
        return -1;

    /* 84B9702C-702C-702C-A6A6-A6A6A6A6A6A6 */
    guid->data1 =
        ((uint32_t)bytes[0]) << 24 |
        ((uint32_t)bytes[1]) << 16 |
        ((uint32_t)bytes[2]) << 8 |
        ((uint32_t)bytes[3]);

    guid->data2 = ((uint32_t)bytes[4]) << 8 | ((uint32_t)bytes[5]);
    guid->data3 = ((uint32_t)bytes[6]) << 8 | ((uint32_t)bytes[7]);

    guid->data4[0] = bytes[8];
    guid->data4[1] = bytes[9];
    guid->data4[2] = bytes[10];
    guid->data4[3] = bytes[11];
    guid->data4[4] = bytes[12];
    guid->data4[5] = bytes[13];
    guid->data4[6] = bytes[14];
    guid->data4[7] = bytes[15];

    return 0;
}

int guid_init_str(guid_t* guid, const char* str)
{
    int ret = -1;

    if (!str || guid_valid_str(str) != 0)
        goto done;

    uint8_t bytes[GUID_BYTES];
    size_t index = 0;
    const char* s = str;

    while (*s)
    {
        uint8_t byte;

        if (*s == '-')
        {
            s++;
            continue;
        }

        /* fail on overflow */
        if (index == GUID_BYTES)
            goto done;

        if (hexstr_scan_byte(s, &byte) < 0)
            goto done;

        bytes[index++] = byte;
        s += 2;
    }

    if (index != GUID_BYTES)
        goto done;

    ret = guid_init_bytes(guid, bytes);

done:
    return ret;
}

int guid_get_xy(const guid_t* guid, uint64_t* x, uint64_t* y)
{
    if (!guid || !x || !y)
        return -1;

    /* 84B9702C-702C-702C-A6A6-A6A6A6A6A6A6 */

    *x = ((uint64_t)guid->data1) |
        (((uint64_t)guid->data2) << 32) |
        (((uint64_t)guid->data3) << 48);

    *y = ((uint64_t)guid->data4[0]) |
        (((uint64_t)guid->data4[1]) << 8) |
        (((uint64_t)guid->data4[2]) << 16) |
        (((uint64_t)guid->data4[3]) << 24) |
        (((uint64_t)guid->data4[4]) << 32) |
        (((uint64_t)guid->data4[5]) << 40) |
        (((uint64_t)guid->data4[6]) << 48) |
        (((uint64_t)guid->data4[7]) << 56);

    return 0;
}

int guid_get_bytes(const guid_t* guid, uint8_t bytes[16])
{
    if (!guid || !bytes)
        return -1;

    // guid->data1:
    bytes[0] = (guid->data1 >> 24);
    bytes[1] = ((guid->data1 >> 16) & 0x000000ff);
    bytes[2] = ((guid->data1 >> 8) & 0x000000ff);
    bytes[3] = (guid->data1 & 0x000000ff);

    // guid->data2:
    bytes[4] = ((guid->data2 >> 8) & 0x000000ff);
    bytes[5] = (guid->data2 & 0x000000ff);

    // guid->data3:
    bytes[6] = ((guid->data3 >> 8) & 0x000000ff);
    bytes[7] = (guid->data3 & 0x000000ff);

    // guid->data4:
    bytes[8] = guid->data4[0];
    bytes[9] = guid->data4[1];
    bytes[10] = guid->data4[2];
    bytes[11] = guid->data4[3];
    bytes[12] = guid->data4[4];
    bytes[13] = guid->data4[5];
    bytes[14] = guid->data4[6];
    bytes[15] = guid->data4[7];

    return 0;
}

int guid_format(guid_string_t* str, const guid_t* guid)
{
    size_t n = 0;
    size_t m = 0;
    size_t i;
    char* buf;

    if (!str || !guid)
        return -1;

    buf = str->buf;

    n += hexstr_format_byte(&buf[n], (guid->data1 & 0xFF000000) >> 24);
    n += hexstr_format_byte(&buf[n], (guid->data1 & 0x00FF0000) >> 16);
    n += hexstr_format_byte(&buf[n], (guid->data1 & 0x0000FF00) >> 8);
    n += hexstr_format_byte(&buf[n], guid->data1 & 0x000000FF);
    buf[n++] = '-';

    n += hexstr_format_byte(&buf[n], (guid->data2 & 0xFF00) >> 8);
    n += hexstr_format_byte(&buf[n], guid->data2 & 0x00FF);
    buf[n++] = '-';

    n += hexstr_format_byte(&buf[n], (guid->data3 & 0xFF00) >> 8);
    n += hexstr_format_byte(&buf[n], guid->data3 & 0x00FF);
    buf[n++] = '-';

    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    buf[n++] = '-';

    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    n += hexstr_format_byte(&buf[n], guid->data4[m++]);
    buf[n++] = '\0';

    for (i = 0; i < n; i++)
        buf[i] = _tolower(buf[i]);

    return 0;
}

int guid_valid_str(const char* str)
{
    int ret = -1;
    size_t i;

    /* Example: 9f2d4c84-a449-4b54-91cd-003b58397b56 */

    if (strlen(str) != GUID_STRING_LENGTH)
        goto done;

    for (i = 0; i < GUID_STRING_LENGTH; i++)
    {
        char c = str[i];

        if (i == 8 || i == 13 || i == 18 || i == 23)
        {
            if (c != '-')
                goto done;
        }
        else
        {
            if (!isxdigit(c))
                goto done;
        }
    }

    ret = 0;

done:
    return ret;
}

bool guid_null(const guid_t* guid)
{
    if (guid->data1 || guid->data2 || guid->data3)
        return false;

    if (guid->data4[0] || guid->data4[1] || guid->data4[2] || guid->data4[3] ||
        guid->data4[4] || guid->data4[5] || guid->data4[6] || guid->data4[7])
    {
        return true;
    }

    return true;
}

void guid_clear(guid_t* guid)
{
    memset(guid, 0, sizeof(guid_t));
}

bool guid_equal(const guid_t* x, const guid_t* y)
{
    return memcmp(x, y, sizeof(guid_t)) == 0;
}

void guid_dump(const guid_t* guid)
{
    if (guid)
    {
        guid_string_t str;
        guid_format(&str, guid);
        printf("%s", str.buf);
    }
}
