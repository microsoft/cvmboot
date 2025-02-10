// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <string.h>

typedef signed long long ssize_t;

char* getenv(const char* name);

void* malloc(size_t size);

void free(void* ptr);

void* calloc(size_t nmemb, size_t size);

void* realloc(void* ptr, size_t size);

#endif /* _STDLIB_H */
