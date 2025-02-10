// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

void* memset(void* s, int c, size_t n);

void* memcpy(void* dest, const void* src, size_t n);

int memcmp(const void* s1, const void* s2, size_t n);

void* memmove(void* dest, const void* src, size_t n);

char* strcpy(char* dest, const char* src);

int strcmp(const char* s1, const char* s2);

size_t strlen(const char* s);

int strncmp(const char* s1, const char* s2, size_t n);

char* strchr(const char* s, int c);

char* strstr(const char* haystack, const char* needle);

char* strncat(char* dest, const char* src, size_t n);

char* strtok_r(char* str, const char* delim, char** saveptr);

char *strcat(char *dest, const char *str);

size_t strlcpy(char* dest, const char* src, size_t size);

size_t strlcat(char* dest, const char* src, size_t size);

char* strdup(const char* s);

char* strncpy(char* dest, const char* src, size_t n);

#ifdef _GNU_SOURCE
void* memmem(
    const void* haystack,
    size_t haystacklen,
    const void* needle,
    size_t needlelen);
#endif

size_t strspn(const char* s, const char* accept);

size_t strcspn(const char* s, const char* reject);

unsigned long int strtoul(const char* nptr, char** endptr, int base);

long int strtol(const char* nptr, char** endptr, int base);

double strtod(const char* nptr, char** endptr);

#endif /* _STRING_H */
