// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_STRINGS_H
#define _CVMBOOT_UTILS_STRINGS_H

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

size_t strlcpy(char* dest, const char* src, size_t size);

size_t strlcat(char* dest, const char* src, size_t size);

size_t strlcpy2(char* dest, const char* src1, const char* src2, size_t size);

size_t strlcpy3(
    char* dest,
    const char* src1,
    const char* src2,
    const char* src3,
    size_t size);

int str2u32(const char* s, uint32_t* x);

// Remove leading (left) whitespace:
char* strltrim(char* str);

// Remove trailing (right) whitespace:
char* strrtrim(char* str);

#endif /* _CVMBOOT_UTILS_STRINGS_H */
