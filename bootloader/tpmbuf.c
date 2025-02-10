// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include "tpmbuf.h"
#include "byteorder.h"

void TPMBufInit(
    TPMBuf* self)
{
    memset(self->data, 0xDD, sizeof(self->data));
    self->size = 0;
    self->offset = 0;
    self->cap = sizeof(self->data);
    self->error = 0;
}

void TPMBufPack(
    TPMBuf* self,
    const void* data,
    UINT32 size)
{
    UINT32 r = self->cap - self->size;

    if (self->error)
        return;

    if (size > r)
    {
        self->error = 1;
        return;
    }

    memcpy(&self->data[self->size], (void*)data, size);
    self->size += size;
}

void TPMBufPackU8(
    TPMBuf* self,
    UINT8 x)
{
    TPMBufPack(self, (const char*)&x, sizeof(x));
}

void TPMBufPackU16(
    TPMBuf* self,
    UINT16 x)
{
    UINT16 tmp = ByteSwapU16(x);
    TPMBufPack(self, (const char*)&tmp, sizeof(tmp));
}

void TPMBufPackU32(
    TPMBuf* self,
    UINT32 x)
{
    UINT32 tmp = ByteSwapU32(x);
    TPMBufPack(self, (const char*)&tmp, sizeof(tmp));
}

#if 0
void TPMBufPackU64(
    TPMBuf* self,
    UINT64 x)
{
    UINT64 tmp = ByteSwapU64(x);
    TPMBufPack(self, (const char*)&tmp, sizeof(tmp));
}
#endif

void TPMBufUnpack(
    TPMBuf* self,
    void* data,
    UINT32 size)
{
    UINT32 r = self->size - self->offset;

    if (self->error)
        return;

    if (size > r)
    {
        self->error = 1;
        return;
    }

    memcpy(data, &self->data[self->offset], size);
    self->offset += size;
}

void TPMBufUnpackU8(
    TPMBuf* self,
    UINT8* x)
{
    UINT32 r = self->size - self->offset;

    if (self->error)
        return;

    if (sizeof(*x) > r)
    {
        self->error = 1;
        return;
    }

    memcpy(x, &self->data[self->offset], sizeof(*x));
    self->offset += sizeof(*x);
}

void TPMBufUnpackU16(
    TPMBuf* self,
    UINT16* x)
{
    UINT16 tmp;
    UINT32 r = self->size - self->offset;

    if (self->error)
        return;

    if (sizeof(tmp) > r)
    {
        self->error = 1;
        return;
    }

    memcpy(&tmp, &self->data[self->offset], sizeof(tmp));
    self->offset += sizeof(tmp);
    *x = ByteSwapU16(tmp);
}

void TPMBufUnpackU32(
    TPMBuf* self,
    UINT32* x)
{
    UINT32 tmp;
    UINT32 r = self->size - self->offset;

    if (self->error)
        return;

    if (sizeof(tmp) > r)
    {
        self->error = 1;
        return;
    }

    memcpy(&tmp, &self->data[self->offset], sizeof(tmp));
    self->offset += sizeof(tmp);
    *x = ByteSwapU32(tmp);
}

#if 0
void TPMBufUnpackU64(
    TPMBuf* self,
    UINT64* x)
{
    UINT64 tmp;
    UINT32 r = self->size - self->offset;

    if (self->error)
        return;

    if (sizeof(tmp) > r)
    {
        self->error = 1;
        return;
    }

    memcpy(&tmp, &self->data[self->offset], sizeof(tmp));
    self->offset += sizeof(tmp);
    *x = ByteSwapU64(tmp);
}
#endif
