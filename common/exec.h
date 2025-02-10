// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_EXEC_H
#define _CVMBOOT_COMMON_EXEC_H

#include <stddef.h>
#include <stdbool.h>
#include "buf.h"

void execf_set_trace(bool flag);

__attribute__((format(printf, 2, 3)))
int execf(buf_t* buf, const char* fmt, ...);

/* like execf() but return on error rather than exiting */
__attribute__((format(printf, 2, 3)))
int execf_return(buf_t* buf, const char* fmt, ...);

#endif /* _CVMBOOT_COMMON_EXEC_H */
