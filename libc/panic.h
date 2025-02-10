// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _LIBC_PANIC_H
#define _LIBC_PANIC_H

void __libc_panic(const char* func, unsigned int line);

void __libc_warn(const char* msg);

#define LIBC_PANIC __libc_panic(__FUNCTION__, __LINE__)

#endif /* _LIBC_PANIC_H */
