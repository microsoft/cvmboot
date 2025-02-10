// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "path.h"
#include <utils/strings.h>

const char* makepath2(
    path_t* path,
    const char* s1,
    const char* s2)
{
    *path->buf = '\0';
    strlcat(path->buf, s1, PATH_MAX);
    strlcat(path->buf, "/", PATH_MAX);
    strlcat(path->buf, s2, PATH_MAX);
    return path->buf;
}

const char* makepath3(
    path_t* path,
    const char* s1,
    const char* s2,
    const char* s3)
{
    *path->buf = '\0';
    strlcat(path->buf, s1, PATH_MAX);
    strlcat(path->buf, "/", PATH_MAX);
    strlcat(path->buf, s2, PATH_MAX);
    strlcat(path->buf, "/", PATH_MAX);
    strlcat(path->buf, s3, PATH_MAX);
    return path->buf;
}

const char* makepath4(
    path_t* path,
    const char* s1,
    const char* s2,
    const char* s3,
    const char* s4)
{
    *path->buf = '\0';
    strlcat(path->buf, s1, PATH_MAX);
    strlcat(path->buf, "/", PATH_MAX);
    strlcat(path->buf, s2, PATH_MAX);
    strlcat(path->buf, "/", PATH_MAX);
    strlcat(path->buf, s3, PATH_MAX);
    strlcat(path->buf, "/", PATH_MAX);
    strlcat(path->buf, s4, PATH_MAX);
    return path->buf;
}
