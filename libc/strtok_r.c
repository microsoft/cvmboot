// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>

char* strtok_r(char* str, const char* delim, char** saveptr)
{
    char* p = str;
    char* end;

    if (str)
        p = str;
    else if (*saveptr)
        p = *saveptr;
    else
        return NULL;

    /* Find start of next token */
    while (*p && strchr(delim, *p))
        p++;

    /* Find the end of the next token */
    for (end = p; *end && !strchr(delim, *end); end++)
        ;

    if (p == end)
        return NULL;

    if (*end)
    {
        *end++ = '\0';
        *saveptr = end;
    }
    else
        *saveptr = NULL;

    return p;
}
