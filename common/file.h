// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_FILE_H
#define _CVMBOOT_COMMON_FILE_H

#include <stddef.h>

int load_file(const char* path, void** data_out, size_t* size_out);

int write_file(const char* path, const void* data, size_t size);

#endif /* _CVMBOOT_COMMON_FILE_H */
