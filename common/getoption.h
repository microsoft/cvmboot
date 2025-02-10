// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_GETOPTION_H
#define _CVMBOOT_COMMON_GETOPTION_H

#include <stddef.h>
#include <utils/err.h>

int getoption(
    int* argc,
    const char* argv[],
    const char* opt,
    const char** optarg,
    err_t* err);

#endif /* _CVMBOOT_COMMON_GETOPTION_H */
