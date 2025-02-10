// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdlib.h>

void qsort(
    void *base,
    size_t nmemb,
    size_t size,
    int (*compar)(const void *, const void *))
{
    size_t i;
    size_t j;
    size_t k;
    size_t n;

    if (nmemb == 0)
        return;

    n = nmemb - 1;

    for (i = 0; i < nmemb - 1; i++)
    {
        int swapped = 0;

        for (j = 0; j < n; j++)
        {
            unsigned char* lhs = (unsigned char*)base + (j  * size);
            unsigned char* rhs = (unsigned char*)base + ((j+1)  * size);

            if (compar(lhs, rhs) > 0)
            {
                for (k = 0; k < size; k++)
                {
                    unsigned char t = lhs[k];
                    lhs[k] = rhs[k];
                    rhs[k] = t;
                }

                swapped = 1;
            }
        }

        if (!swapped)
            break;

        n--;
    }
}
