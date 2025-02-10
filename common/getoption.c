// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <stdio.h>
#include "getoption.h"

int getoption(
    int* argc,
    const char* argv[],
    const char* opt,
    const char** optarg,
    err_t* err)
{
    int ret = 0;
    size_t olen = strlen(opt);

    if (optarg)
        *optarg = NULL;

    if (!argv || !opt || !err)
    {
        err_format(err, "bad argument");
        ret = -1;
        goto done;
    }

    for (int i = 0; i < *argc;)
    {
        if (strcmp(argv[i], opt) == 0)
        {
            if (optarg)
            {
                if (i + 1 == *argc)
                {
                    err_format(err, "%s: missing option argument", opt);
                    ret = -1;
                    goto done;
                }

                *optarg = argv[i + 1];
                memmove(
                    &argv[i], &argv[i + 2], (*argc - i - 1) * sizeof(char*));
                (*argc) -= 2;
                goto done;
            }
            else
            {
                memmove(&argv[i], &argv[i + 1], (*argc - i) * sizeof(char*));
                (*argc)--;
                goto done;
            }
        }
        else if (strncmp(argv[i], opt, olen) == 0 && argv[i][olen] == '=')
        {
            if (!optarg)
            {
                err_format(err, "%s: extraneous '='", opt);
                ret = -1;
                goto done;
            }

            *optarg = &argv[i][olen + 1];
            memmove(&argv[i], &argv[i + 1], (*argc - i) * sizeof(char*));
            (*argc)--;
            goto done;
        }
        else
        {
            i++;
        }
    }

    /* Not found! */
    ret = 1;

done:
    return ret;
}
