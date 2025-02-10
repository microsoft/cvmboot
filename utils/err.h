// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_ERR_H
#define _CVMBOOT_UTILS_ERR_H

#define ERR_INITIALIZER { { '\0' } }
#define ERR_BUF_SIZE 1024

typedef struct
{
    char buf[ERR_BUF_SIZE];
}
err_t;

static __inline__ void err_clear(err_t* err)
{
    if (err)
        *err->buf = '\0';
}

__attribute__((format(printf, 2, 3)))
void err_format(err_t* err, const char* fmt, ...);

#endif /* _CVMBOOT_UTILS_ERR_H */
