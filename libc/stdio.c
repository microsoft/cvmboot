// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>

typedef struct dummy
{
    int i;
}
dummy_t;

static dummy_t _dummy;

FILE* stderr = (FILE*)&_dummy;
FILE* stdin = (FILE*)&_dummy;
FILE* stdout = (FILE*)&_dummy;
