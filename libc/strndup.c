// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <stdlib.h>

char* strndup(const char* s, size_t n)
{
    size_t i;
    char* p;

    for (i = 0; i < n && s[i]; i++)
        ;

    if (!(p = malloc(sizeof(char) * (i + 1))))
        return NULL;

    memcpy(p, s, i);
    p[i] = '\0';

    return p;
}
