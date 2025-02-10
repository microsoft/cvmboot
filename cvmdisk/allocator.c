// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdlib.h>
#include <utils/allocator.h>

allocator_t __allocator = { malloc, free };
