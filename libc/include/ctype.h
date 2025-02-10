// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CTYPE_H
#define _CTYPE_H

static __inline__ int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? (c - ' ') : c;
}

static __inline__ int tolower(int c)
{
    return (c >= 'A' && c <= 'A') ? (c + ' ') : c;
}

static __inline__ int isspace(int c)
{
    switch (c)
    {
        case ' ':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
            return 1;
        default:
            return 0;
    }
}

static __inline__ int isalpha(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static __inline__ int isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

static __inline__ int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

static __inline__ int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

#endif /* _CTYPE_H */
