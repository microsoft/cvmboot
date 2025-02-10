// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "strings.h"

size_t strlcpy(char* dest, const char* src, size_t size)
{
    const char* start = src;

    if (size)
    {
        char* end = dest + size - 1;

        while (*src && dest != end)
            *dest++ = (char)*src++;

        *dest = '\0';
    }

    while (*src)
        src++;

    return (size_t)(src - start);
}

size_t strlcat(char* dest, const char* src, size_t size)
{
    size_t n = 0;

    if (size)
    {
        char* end = dest + size - 1;

        while (*dest && dest != end)
        {
            dest++;
            n++;
        }

        while (*src && dest != end)
        {
            n++;
            *dest++ = *src++;
        }

        *dest = '\0';
    }

    while (*src)
    {
        src++;
        n++;
    }

    return n;
}

size_t strlcpy2(char* dest, const char* src1, const char* src2, size_t size)
{
    if (strlcpy(dest, src1, size) >= size)
        return size;

    return strlcat(dest, src2, size);
}

size_t strlcpy3(
    char* dest,
    const char* src1,
    const char* src2,
    const char* src3,
    size_t size)
{
    if (strlcpy(dest, src1, size) >= size)
        return size;

    if (strlcat(dest, src2, size) >= size)
        return size;

    return strlcat(dest, src3, size);
}


int str2u32(const char* s, uint32_t* x)
{
    const char* p = s + strlen(s);
    size_t r = 1;
    size_t n = 0;

    while (p != s)
    {
        int c = p[-1];

        if (!isdigit(c))
            return -1;

        int d = c - '0';
        n += d * r;
        r *= 10;
        p--;
    }

    *x = n;

    return 0;
}

char* strltrim(char* str)
{
    char* p = str;

    while (*p && isspace(*p))
        p++;

    memmove(str, p, strlen(p) + 1);
    return str;
}

char* strrtrim(char* str)
{
    char* p = str + strlen(str);

    while (p != str && isspace(p[-1]))
        *--p = '\0';

    return str;
}
