// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "key.h"
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>

int create_rsa_key_from_exponent_and_modulus(
    public_rsa_key_t** key,
    const void* exponent,
    size_t exponent_size,
    const void* modulus,
    size_t modulus_size)
{
    int ret = -1;
    BIGNUM* e = NULL; /* exponent */
    BIGNUM* n = NULL; /* modulus */
    OSSL_PARAM_BLD* bld = NULL;
    EVP_PKEY* pkey = NULL;
    OSSL_PARAM* params = NULL;
    EVP_PKEY_CTX* ctx = NULL;

    /* Check parameters */
    if (!key || !exponent || !exponent_size || !modulus || !modulus_size)
        goto done;

    /* Create the exponent big number */
    if (!(e = BN_bin2bn(exponent, exponent_size, NULL)))
        goto done;

    /* Create the modulus big number */
    if (!(n = BN_bin2bn(modulus, modulus_size, NULL)))
        goto done;

    /* Build the RSA public key from the exponent and modulus */
    {
        if (!(bld = OSSL_PARAM_BLD_new()))
            goto done;

        if (!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_N, n))
            goto done;

        if (!OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_RSA_E, e))
            goto done;

        if (!(params = OSSL_PARAM_BLD_to_param(bld)))
            goto done;

        if (!(ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL)))
            goto done;

        if (!(pkey = EVP_PKEY_new()))
            goto done;

        if (EVP_PKEY_fromdata_init(ctx) <= 0)
            goto done;

        if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0)
            goto done;
    }

    *key = (public_rsa_key_t*)pkey;
    pkey = NULL;

    ret = 0;

done:

    if (e)
        BN_free(e);

    if (n)
        BN_free(n);

    if (pkey)
        EVP_PKEY_free(pkey);

    if (ctx)
        EVP_PKEY_CTX_free(ctx);

    if (params)
        OSSL_PARAM_free(params);

    return ret;
}
