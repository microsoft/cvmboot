// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <ctype.h>

int strcasecmp(const char* s1, const char* s2)
{
    while (*s1 && *s2 && toupper(*s1) == toupper(*s2))
    {
        s1++;
        s2++;
    }

    if (*s1)
        return 1;

    if (*s2)
        return -1;

    return 0;
}
