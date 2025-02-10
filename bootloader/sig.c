// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <openssl/evp.h>
#include <utils/sha256.h>
#include "sig.h"
#include "key.h"
#include "console.h"

int sig_verify(const sig_t* sig)
{
    int ret = -1;
    public_rsa_key_t* pubkey = NULL;

    /* Create the RSA key from the PEM data (include zero-terminator) */
    if (create_rsa_key_from_exponent_and_modulus(
        &pubkey,
        sig->exponent,
        sig->exponent_size,
        sig->modulus,
        sig->modulus_size) < 0)
    {
        Print(L"Failed to create public key from signature structure");
        pause(NULL);
        goto done;
    }

    /* Verify the key */
    if (rsa_verify(
        pubkey,
        (const sha256_t*)sig->digest,
        sig->signature,
        sig->signature_size) != 0)
    {
        pause("rsa_verify() failed");
        goto done;
    }

    ret = 0;

done:

    if (pubkey)
        free_public_rsa_key(pubkey);

    return ret;
}
