// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "find.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <utils/strings.h>

int find(const char* dirname, strarr_t* names)
{
    int ret = -1;
    DIR* dir = NULL;
    struct dirent* ent;
    strarr_t dirs = STRARR_INITIALIZER;

    if (!(dir = opendir(dirname)))
        goto done;

    while ((ent = readdir(dir)))
    {
        char fullpath[PATH_MAX];
        struct stat statbuf;

        if (strcmp(ent->d_name, ".") == 0)
            continue;

        if (strcmp(ent->d_name, "..") == 0)
            continue;

        strlcpy3(fullpath, dirname, "/", ent->d_name, PATH_MAX);

        if (strarr_append(names, fullpath) < 0)
            goto done;

        if (lstat(fullpath, &statbuf) < 0)
            goto done;

        if (S_ISDIR(statbuf.st_mode) && !S_ISLNK(statbuf.st_mode))
        {
            if (strarr_append(&dirs, fullpath) < 0)
                goto done;
        }
    }

    closedir(dir);
    dir = NULL;

    for (size_t i = 0; i < dirs.size; i++)
    {
        if (find(dirs.data[i], names) < 0)
            goto done;
    }

    ret = 0;

done:

    if (dir)
        closedir(dir);

    strarr_release(&dirs);
    return ret;
}
