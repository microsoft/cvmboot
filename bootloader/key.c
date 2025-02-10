// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <openssl/bio.h>
#include <openssl/engine.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <utils/sha256.h>
#include <openssl/param_build.h>
#include "key.h"
#include "console.h"

#include <openssl/core_names.h>

static int _initialized = 0;

static void _initialize(void)
{
    if (!_initialized)
    {
        OPENSSL_no_config();
        _initialized = 1;
    }
}

void crypto_initialize(void)
{
    _initialize();
}

int rsa_verify(
    public_rsa_key_t* key,
    const sha256_t* hash,
    const uint8_t* signature,
    size_t signature_size)
{
    int ret = -1;
    EVP_PKEY_CTX* ctx = NULL;

    /* Check for null parameters */
    if (!key || !hash || !signature || !signature_size)
    {
        pause("rsa_verify() bad parameters");
        goto done;
    }

    /* Initialize openssl */
    _initialize();

    /* Create signing context */
    if (!(ctx = EVP_PKEY_CTX_new((EVP_PKEY*)key, NULL)))
    {
        pause("EVP_PKEY_CTX_new() failed");
        goto done;
    }

    /* Initialize the signing context */
    if (EVP_PKEY_verify_init(ctx) <= 0)
    {
        pause("EVP_PKEY_verify_init() failed");
        goto done;
    }

    /* Set the MD type for the signing operation */
    if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) <= 0)
    {
        pause("EVP_PKEY_CTX_set_signature_md() failed");
        goto done;
    }

    /* Compute the signature */
    if (!EVP_PKEY_verify(
        ctx,
        signature,
        signature_size,
        hash->data,
        sizeof(sha256_t)))
    {
        pause("EVP_PKEY_verify() failed");
        goto done;
    }

    ret = 0;

done:

    if (ctx)
        EVP_PKEY_CTX_free(ctx);

    return ret;
}

void free_public_rsa_key(public_rsa_key_t* key)
{
    if (key)
        EVP_PKEY_free((EVP_PKEY*)key);
}
