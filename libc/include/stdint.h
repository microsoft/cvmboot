// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _STDINT_H
#define _STDINT_H

#include <efibind.h> /* uint64_t, int64_t, etc. */

typedef long long intmax_t;
typedef unsigned long long uintmax_t;

#if 0 /* defined by <efibind.h> */
typedef unsigned long long  uint64_t;
typedef long long int64_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned char uint8_t;
typedef char int8_t;
#endif

#define UINT64_MAX (0xffffffffffffffffu)

#endif /* _STDINT_H */
