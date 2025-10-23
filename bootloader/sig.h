// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_BOOTLOADER_SIG_H
#define _CVMBOOT_BOOTLOADER_SIG_H

#include <utils/sig.h>
#include <utils/sha256.h>
#include <stddef.h>

/* In-memory signature structure used by bootloader with computed signer */
typedef struct bootloader_sig
{
    /* SHA-256 digest of the data being signed */
    uint8_t digest[SIG_DIGEST_SIZE];

    /* Computed signer hash: SHA256(exponent||modulus) */
    uint8_t signer[SIG_SIGNER_SIZE];

    /* the signature */
    uint8_t signature[SIG_MAX_SIGNATURE_SIZE];

    /* the size of the signature in bytes */
    uint64_t signature_size;

    /* the exponent of the signing key */
    uint8_t exponent[SIG_MAX_EXPONENT_SIZE];

    /* the size of the exponent */
    uint64_t exponent_size;

    /* the modulus of the signing key */
    uint8_t modulus[SIG_MAX_MODULUS_SIZE];

    /* the size of the modulus */
    uint64_t modulus_size;
} bootloader_sig_t;

/* Convert and verify signature, returning the bootloader signature structure */
int sig_convert_and_verify(const sig_t* disk_sig, bootloader_sig_t* bootloader_sig);

#endif /* _CVMBOOT_BOOTLOADER_SIG_H */
