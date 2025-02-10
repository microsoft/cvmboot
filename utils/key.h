// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_KEY_H
#define _CVMBOOT_UTILS_KEY_H

#include <stdint.h>
#include <stddef.h>

typedef struct private_key private_rsa_key_t;
typedef struct public_key public_rsa_key_t;

int create_rsa_key_from_exponent_and_modulus(
    public_rsa_key_t** key,
    const void* exponent,
    size_t exponent_size,
    const void* modulus,
    size_t modulus_size);

#endif /* _CVMBOOT_UTILS_KEY_H */
