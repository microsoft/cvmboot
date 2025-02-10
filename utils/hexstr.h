// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_HEXSTR_H
#define _CVMBOOT_UTILS_HEXSTR_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* format data as a hexidecimal string for printing */
int hexstr_format(char* str, size_t str_size, const void* data, size_t size);

/* scan a hexidecimal string into binary form */
ssize_t hexstr_scan(const char* str, uint8_t* data, size_t size);

/* format a single byte into hexadecimal format */
size_t hexstr_format_byte(char buf[3], uint8_t x);

/* scan a single hex byte from the given hexadecimal string buffer */
int hexstr_scan_byte(const char* buf, uint8_t* byte);

/* dump a hexidecimal string to standard output */
void hexstr_dump(const void* s, size_t n);

#endif /* _CVMBOOT_UTILS_HEXSTR_H */
