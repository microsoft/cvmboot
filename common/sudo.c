// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "sudo.h"
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>
#include <utils/strings.h>

int sudo_get_uid_gid(uid_t* uid, gid_t* gid)
{
    int ret = -1;
    const char* sudo_uid;
    const char* sudo_gid;
    *uid = getuid();
    *gid = getgid();
    size_t found = 0;

    if ((sudo_uid = getenv("SUDO_UID")))
    {
        *uid = atoi(sudo_uid);
        found++;
    }

    if ((sudo_gid = getenv("SUDO_GID")))
    {
        *gid = atoi(sudo_gid);
        found++;
    }

    if (found != 0 && found != 2)
        goto done;

    ret = 0;

done:
    return ret;
}

int sudo_get_home_dir(char home[PATH_MAX])
{
    int ret = -1;
    uid_t uid;
    gid_t gid;
    struct passwd pw;
    struct passwd* pwp;
    char buf[NSS_BUFLEN_PASSWD];

    *home = '\0';

    if (sudo_get_uid_gid(&uid, &gid) < 0)
        goto done;

    setpwent();

    while (getpwent_r(&pw, buf, sizeof(buf), &pwp) == 0)
    {
        if (pw.pw_uid == uid && pw.pw_gid == gid)
        {
            strlcpy(home, pw.pw_dir, PATH_MAX);
            ret = 0;
            break;
        }
    }

    endpwent();

done:
    return ret;
}
