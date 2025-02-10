// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_COMMON_KEY_H
#define _CVMBOOT_COMMON_KEY_H

#include <stdint.h>
#include <stdbool.h>
#include <openssl/evp.h>
#include <utils/key.h>

int read_private_rsa_key(
    private_rsa_key_t** key,
    const void* pem_data,
    size_t pem_size);

int read_public_rsa_key(
    public_rsa_key_t** key,
    const void* pem_data,
    size_t pem_size);

ssize_t rsa_sign(
    private_rsa_key_t* key,
    const sha256_t* sha256,
    uint8_t* signature,
    size_t max_signature_size);

ssize_t key_get_exponent(
    public_rsa_key_t* key,
    uint8_t* buffer,
    size_t max_buffer_size);

ssize_t key_get_modulus(
    public_rsa_key_t* key,
    uint8_t* buffer,
    size_t max_buffer_size);

int rsa_verify(
    public_rsa_key_t* key,
    const sha256_t* hash,
    const uint8_t* signature,
    size_t signature_size);

void free_private_rsa_key(private_rsa_key_t* key);

void free_public_rsa_key(public_rsa_key_t* key);

#endif /* _CVMBOOT_COMMON_KEY_H */
