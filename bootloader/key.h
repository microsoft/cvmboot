// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_KEY_H
#define _CVMBOOT_BOOTLOADER_KEY_H

#include <stdint.h>
#include <stdbool.h>
#include <openssl/evp.h>
#include <utils/sha256.h>
#include <utils/key.h>

void crypto_initialize(void);

int rsa_verify(
    public_rsa_key_t* key,
    const sha256_t* hash,
    const uint8_t* signature,
    size_t signature_size);

void free_public_rsa_key(public_rsa_key_t* key);

#endif /* _CVMBOOT_BOOTLOADER_KEY_H */
