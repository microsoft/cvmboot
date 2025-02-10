// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_ERR_H
#define _CVMBOOT_COMMON_ERR_H

#include <stdbool.h>

void err_set_arg0(const char* arg0);

void err_show_file_line_func(bool flag);

__attribute__((format(printf, 4, 5)))
void __err(
    const char* file,
    unsigned int line,
    const char* func,
    const char* fmt,
    ...);

__attribute__((format(printf, 4, 5)))
void __err_noexit(
    const char* file,
    unsigned int line,
    const char* func,
    const char* fmt,
    ...);

#define ERR(FMT...) __err(__FILE__, __LINE__, __FUNCTION__, FMT)
#define ERR_NOEXIT(FMT...) __err_noexit(__FILE__, __LINE__, __FUNCTION__, FMT)

#endif /* _CVMBOOT_COMMON_ERR_H */
