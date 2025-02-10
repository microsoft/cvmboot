// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <ctype.h>

int strncasecmp(const char* s1, const char* s2, size_t n)
{
    while (n && *s1 && *s2 && toupper(*s1) == toupper(*s2))
    {
        n--;
        s1++;
        s2++;
    }

    if (n == 0)
        return 0;

    if (!*s1)
        return -1;
    else if (!*s2)
        return 1;
    else
        return toupper(*s1) - toupper(*s2);
}
