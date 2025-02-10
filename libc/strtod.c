// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <stdbool.h>
#include <ctype.h>

double strtod(const char* nptr, char** endptr)
{
    const char* p;
    bool negative = false;
    unsigned long x = 0;
    unsigned long y = 0;
    unsigned long n = 0;
    unsigned long decimal_places = 1;
    size_t i;

    if (endptr)
        *endptr = (char*)nptr;

    if (!nptr)
        return 0;

    /* Set scanning pointer to nptr */
    p = nptr;

    /* Skip any leading whitespace */
    while (isspace(*p))
        p++;

    /* Handle '+' and '-' */
    if (p[0] == '+')
    {
        p++;
    }
    else if (p[0] == '-')
    {
        negative = true;
        p++;
    }

    /* Skip any whitespace */
    while (isspace(*p))
        p++;

    /* get x of x.y */
    {
        char* end = NULL;
        x = strtoul(p, &end, 10);
        p = end;

        if (endptr)
            *endptr = (char*)p;
    }

    /* if end of string */
    if (!*p)
        return (double)x;

    if (*p != '.')
    {
        if (endptr)
            *endptr = (char*)p;
        return (double)x;
    }

    p++;

    /* get y of x.y */
    {
        char* end = NULL;
        y = strtoul(p, &end, 10);
        n = end - p;
        p = end;

        if (endptr)
            *endptr = (char*)p;
    }

    /* if no decimal part */
    if (n == 0)
        return (double)x;

    /* calculate the number of decimal places */
    for (i = 0; i < n; i++)
        decimal_places *= 10;

    double result = (double)x + ((double)y / (double)decimal_places);

    if (negative)
        result = -result;

    return result;
}
