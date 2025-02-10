// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdlib.h>
#include <string.h>

char* strdup(const char* s)
{
    char* p;
    size_t len;

    len = strlen(s);

    if (!(p = malloc(len + 1)))
        return NULL;

    memcpy(p, s, len + 1);

    return p;
}
