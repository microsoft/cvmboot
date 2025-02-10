// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _CVMBOOT_UTILS_SIG_H
#define _CVMBOOT_UTILS_SIG_H

#include <stdint.h>

#define SIG_MAGIC 0x9d2d3be907d34589

#define SIG_VERSION 1

#define SIG_DIGEST_SIZE 32

#define SIG_SIGNER_SIZE 32

/* large enough for a 8192-bit key/signature */
#define SIG_MAX_SIGNATURE_SIZE 1024

#define SIG_MAX_MODULUS_SIZE 1024

#define SIG_MAX_EXPONENT_SIZE 32

/* the file-system signature structure */
typedef struct sig
{
    /* the magic number (must be SIG_MAGIC) */
    uint64_t magic;

    /* the version number (must be SIG_VERSION) */
    uint64_t version;

    /* SHA-256 digest of the data being signed */
    uint8_t digest[SIG_DIGEST_SIZE];

    /* Signer hash of modulus and exponent: SHA256(modulus||exponent) */
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

    /* padding */
    uint8_t padding[1952-32-8];
} sig_t;

_Static_assert(sizeof(sig_t) == 4096, "");

#endif /* _CVMBOOT_UTILS_SIG_H */
