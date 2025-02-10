// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_ERAISE_H
#define _CVMBOOT_CVMDISK_ERAISE_H

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "defs.h"

#define ERAISE(ERRNUM)                                           \
    do                                                           \
    {                                                            \
        ret = ERRNUM;                                            \
        if (ret < 0)                                             \
        {                                                        \
            __eraise(__FILE__, __LINE__, __FUNCTION__, (int)ret); \
            goto done;                                           \
        }                                                        \
    } while (0)

#define ECHECK(ERRNUM)                                           \
    do                                                           \
    {                                                            \
        typeof(ERRNUM) _r_ = ERRNUM;                             \
        if (_r_ < 0)                                             \
        {                                                        \
            ret = (typeof(ret))_r_;                              \
            __eraise(__FILE__, __LINE__, __FUNCTION__, (int)ret); \
            goto done;                                           \
        }                                                        \
    } while (0)

void __eraise(const char* file, uint32_t line, const char* func, int errnum);

#endif /* _CVMBOOT_CVMDISK_ERAISE_H */
