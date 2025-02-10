// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_TPMBUF_H
#define _CVMBOOT_UTILS_TPMBUF_H

#include <efi.h>
#include <efilib.h>

#define __BUF_CAPACITY 4096

typedef struct _TPMBuf
{
    unsigned char data[__BUF_CAPACITY];
    UINT32 size;
    UINT32 offset;
    UINT32 cap;
    int error; /* non-zero if buffer is in an error state */
}
TPMBuf;

void TPMBufInit(TPMBuf* self);

void TPMBufPack(TPMBuf* self, const void* data, UINT32 size);

void TPMBufPackU8(TPMBuf* self, UINT8 x);

void TPMBufPackU16(TPMBuf* self, UINT16 x);

void TPMBufPackU32(TPMBuf* self, UINT32 x);

#if 0
void TPMBufPackU64(TPMBuf* self, UINT64 x);
#endif

void TPMBufUnpack(TPMBuf* self, void* data, UINT32 size);

void TPMBufUnpackU8(TPMBuf* self, UINT8* x);

void TPMBufUnpackU16(TPMBuf* self, UINT16* x);

void TPMBufUnpackU32(TPMBuf* self, UINT32* x);

#if 0
void TPMBufUnpackU64(TPMBuf* self, UINT64* x);
#endif

#endif /* _CVMBOOT_UTILS_TPMBUF_H */
