// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _LIMITS_H
#define _LIMITS_H

#include <stddef.h>
#include <efi.h>
#include <efilib.h>

#define INT_MIN	(-INT_MAX-1)
#define INT_MAX	2147483647
#define PATH_MAX 4096
#define LONG_MAX 0x7fffffffffffffffL
#define ULONG_MAX (2UL*LONG_MAX+1)

#endif /* _LIMITS_H */
