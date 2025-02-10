// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_BYTEORDER_H
#define _CVMBOOT_BOOTLOADER_BYTEORDER_H

#include <efi.h>
#include <efilib.h>

/*
**==============================================================================
**
** IS_BIG_ENDIAN
**
**==============================================================================
*/

static inline BOOLEAN __IsBigEndian()
{
    typedef union _U
    {
        unsigned short x;
        unsigned char bytes[2];
    }
    U;
    static U u = { 0xABCD };
    return u.bytes[0] == 0xAB ? TRUE : FALSE;
}

#if defined(__i386) || defined(__x86_64)
# define IS_BIG_ENDIAN FALSE
#endif

#if !defined(IS_BIG_ENDIAN)
# define IS_BIG_ENDIAN __IsBigEndian()
#endif

/*
**==============================================================================
**
** ByteSwapU16()
** ByteSwapU32()
** ByteSwapU64()
**
**==============================================================================
*/

static inline UINT64 ByteSwapU64(UINT64 x)
{
    if (IS_BIG_ENDIAN)
    {
        return x;
    }
    else
    {
        return
            ((UINT64)((x & 0xFF) << 56)) |
            ((UINT64)((x & 0xFF00) << 40)) |
            ((UINT64)((x & 0xFF0000) << 24)) |
            ((UINT64)((x & 0xFF000000) << 8)) |
            ((UINT64)((x & 0xFF00000000) >> 8)) |
            ((UINT64)((x & 0xFF0000000000) >> 24)) |
            ((UINT64)((x & 0xFF000000000000) >> 40)) |
            ((UINT64)((x & 0xFF00000000000000) >> 56));
    }
}

static inline UINT32 ByteSwapU32(UINT32 x)
{
    if (IS_BIG_ENDIAN)
    {
        return x;
    }
    else
    {
        return
            ((UINT32)((x & 0x000000FF) << 24)) |
            ((UINT32)((x & 0x0000FF00) << 8)) |
            ((UINT32)((x & 0x00FF0000) >> 8)) |
            ((UINT32)((x & 0xFF000000) >> 24));
    }
}

static inline UINT16 ByteSwapU16(UINT16 x)
{
    if (IS_BIG_ENDIAN)
    {
        return x;
    }
    else
    {
        return
            ((UINT16)((x & 0x00FF) << 8)) |
            ((UINT16)((x & 0xFF00) >> 8));
    }
}

#endif /* _CVMBOOT_BOOTLOADER_BYTEORDER_H */
