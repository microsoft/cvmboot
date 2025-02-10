// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_SUDO_H
#define _CVMBOOT_COMMON_SUDO_H

#include <unistd.h>
#include <limits.h>

int sudo_get_uid_gid(uid_t* uid, gid_t* gid);

int sudo_get_home_dir(char home[PATH_MAX]);

#endif /* _CVMBOOT_COMMON_SUDO_H */
