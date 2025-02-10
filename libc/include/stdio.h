// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <efi.h>
#include <efilib.h>

__attribute__((format(printf, 1, 2)))
int printf(const char *format, ...);

int vprintf(const char *format, va_list ap);

__attribute__((format(printf, 3, 4)))
int snprintf(char* str, size_t size, const char* format, ...);

int vsnprintf(char* str, size_t size, const char* format, va_list ap);

int putchar(int c);

int puts(const char *s);

#endif /* _STDIO_H */
