// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_CVMDISK_LOOP_H
#define _CVMBOOT_CVMDISK_LOOP_H

#include <stdint.h>
#include <limits.h>
#include "defs.h"

int loop_parse(const char* path, uint32_t* loopnum, uint32_t* partnum);

int loop_basename(const char* path, char base[PATH_MAX]);

void loop_format(char path[PATH_MAX], uint32_t loopnum, uint32_t partnum);

void losetup(const char* disk, char loop[PATH_MAX]);

void lodetach(const char* loop);

#endif /* _CVMBOOT_CVMDISK_LOOP_H */
