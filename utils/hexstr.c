// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "hexstr.h"

static int _char_to_nibble(char c)
{
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');

    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');

    if (c >= '0' && c <= '9')
        return c - '0';

    return -1;
}

int hexstr_scan_byte(const char* buf, uint8_t* byte)
{
    int hi;
    int lo;

    if (byte)
        *byte = '\0';

    if (!buf || !buf[0] || !buf[1] || !byte)
        return -1;

    if ((hi = _char_to_nibble(buf[0])) < 0)
        return -1;

    if ((lo = _char_to_nibble(buf[1])) < 0)
        return -1;

    *byte = (uint8_t)((hi << 4) | lo);
    return 0;
}

static const char _hexchar[] =
{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

size_t hexstr_format_byte(char buf[3], uint8_t x)
{
    buf[0] = _hexchar[(0xF0 & x) >> 4];
    buf[1] = _hexchar[0x0F & x];
    buf[2] = '\0';
    return 2;
}

ssize_t hexstr_scan(const char* str, uint8_t* buf, size_t buf_size)
{
    ssize_t ret = -1;
    size_t len;
    size_t size = 0;
    size_t i;

    if ((len = strlen(str)) == 0 || (len % 2) != 0)
        goto done;

    if (len / 2 > buf_size)
        goto done;

    for (i = 0; i < len - 1; i += 2)
    {
        uint8_t byte;

        if (hexstr_scan_byte(&str[i], &byte) < 0)
            goto done;

        buf[size++] = byte;
    }

    ret = size;

done:

    return ret;
}

int hexstr_format(
    char* str,
    size_t str_size,
    const void* data,
    size_t size)
{
    int ret = -1;
    const uint8_t* src = data;
    char* dest = str;
    size_t i;

    if (!data || !str)
        goto done;

    if (str_size < (2 * size + 1))
        goto done;

    *dest = '\0';

    for (i = 0; i < size; i++)
        dest += hexstr_format_byte(dest, src[i]);

    ret = 0;

done:

    return ret;
}

void hexstr_dump(const void* s, size_t n)
{
    const unsigned char* p = s;
    size_t i;

    for (i = 0; i < n; i++)
    {
        char buf[3];
        hexstr_format_byte(buf, p[i]);
        printf("%s", buf);
    }

    printf("\n");
}
