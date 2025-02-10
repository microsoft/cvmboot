// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "sharedir.h"
#include <sys/stat.h>
#include <limits.h>
#include <libgen.h>
#include <stdlib.h>
#include "which.h"
#include "eraise.h"

static char _sharedir[PATH_MAX];

int locate_sharedir(const char* arg0)
{
    int ret = 0;
    char progname[PATH_MAX];
    char sharedir[PATH_MAX];
    char* dn;
    struct stat statbuf;

    if (which(arg0, progname) != 0)
        ERAISE(-EINVAL);

    if (!(dn = dirname(progname)))
        ERAISE(-EINVAL);

    snprintf(sharedir, sizeof(sharedir), "%s/../share/cvmboot", dn);

    if (!realpath(sharedir, _sharedir))
        ERAISE(-EINVAL);

    if (stat(_sharedir, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
        ERAISE(-EINVAL);

done:
    return ret;
}

const char* sharedir(void)
{
    return _sharedir;
}
