// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char* strstr(const char* haystack, const char* needle)
{
    size_t len = strlen(needle);
    const char* p;

    for (p = haystack; *p; p++)
    {
        if (strncmp(p, needle, len) == 0)
            return (char*)p;
    }

    return NULL;
}
