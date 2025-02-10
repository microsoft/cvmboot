// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <openssl/bio.h>
#include <openssl/engine.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/core.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <utils/sha256.h>
#include "key.h"

//#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

_Static_assert(OPENSSL_VERSION_NUMBER >= 0x30000000L);

static int _initialized = 0;

static void _initialize(void)
{
    if (!_initialized)
    {
        OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, NULL);
        _initialized = 1;
    }
}

static int _read_rsa_key(
    EVP_PKEY** pkey_out,
    const void* pem_data,
    size_t pem_size,
    int key_type,
    bool priv)
{
    int ret = -1;
    BIO* bio = NULL;
    EVP_PKEY* pkey = NULL;

    /* Check parameters */
    if (!pkey_out || !pem_data || !pem_size)
        goto done;

    *pkey_out = NULL;

    /* Must have pem_size-1 non-zero characters followed by zero-terminator */
    if (strnlen((const char*)pem_data, pem_size) != pem_size - 1)
        goto done;

    /* Initialize OpenSSL */
    _initialize();

    /* Create a BIO object for reading the PEM data */
    if (!(bio = BIO_new_mem_buf(pem_data, (int)pem_size)))
        goto done;

    /* Read the key object */
    if (priv)
    {
        if (!(pkey = PEM_read_bio_PrivateKey(bio, &pkey, NULL, NULL)))
            goto done;
    }
    else
    {
        if (!(pkey = PEM_read_bio_PUBKEY(bio, &pkey, NULL, NULL)))
            goto done;
    }

    /* Verify that it is the right key type */
    if (EVP_PKEY_id(pkey) != key_type)
        goto done;

    /* Initialize the key */
    *pkey_out = pkey;
    pkey = NULL;

    ret = 0;

done:

    if (pkey)
        EVP_PKEY_free(pkey);

    if (bio)
        BIO_free(bio);

    return ret;
}

int read_private_rsa_key(
    private_rsa_key_t** key,
    const void* pem_data,
    size_t pem_size)
{
    return _read_rsa_key(
        (EVP_PKEY**)key, pem_data, pem_size, EVP_PKEY_RSA, true);
}

int read_public_rsa_key(
    public_rsa_key_t** key,
    const void* pem_data,
    size_t pem_size)
{
    return _read_rsa_key(
        (EVP_PKEY**)key, pem_data, pem_size, EVP_PKEY_RSA, false);
}

/* returns the actual signature size */
ssize_t rsa_sign(
    private_rsa_key_t* key,
    const sha256_t* sha256,
    uint8_t* signature,
    size_t max_signature_size)
{
    int ret = -1;
    EVP_PKEY_CTX* ctx = NULL;
    size_t signature_size = max_signature_size;

    /* Check for null parameters */
    if (!key || !sha256 || !signature || max_signature_size == 0)
        goto done;

    /* Initialize OpenSSL */
    _initialize();

    /* Create signing context */
    if (!(ctx = EVP_PKEY_CTX_new((EVP_PKEY*)key, NULL)))
        goto done;

    /* Initialize the signing context */
    if (EVP_PKEY_sign_init(ctx) <= 0)
        goto done;

    /* Set the MD type for the signing operation */
    if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) <= 0)
        goto done;

    /* Determine the size of the signature; fail if buffer is too small */
    {
        if (EVP_PKEY_sign(ctx, NULL, &signature_size,
            (const void*)sha256, sizeof(sha256_t)) <= 0)
        {
            goto done;
        }

        if (signature_size > max_signature_size)
            goto done;
    }

    /* Compute the signature */
    if (EVP_PKEY_sign(ctx, signature, &signature_size,
        (const void*)sha256, sizeof(sha256_t)) <= 0)
    {
            goto done;
    }

    ret = signature_size;

done:

    if (ctx)
        EVP_PKEY_CTX_free(ctx);

    return ret;
}

ssize_t key_get_exponent(
    public_rsa_key_t* key,
    uint8_t* buffer,
    size_t max_buffer_size)
{
    ssize_t ret = -1;
    size_t required_size = 0;
    const BIGNUM* bn = NULL;
    BIGNUM* e = NULL;
    BIGNUM* n = NULL;

    if (!key || !buffer|| !max_buffer_size)
        goto done;

    /* get modulus */
    if (!EVP_PKEY_get_bn_param((EVP_PKEY*)key, OSSL_PKEY_PARAM_RSA_N, &n))
        goto done;

    /* get exponent */
    if (!EVP_PKEY_get_bn_param((EVP_PKEY*)key, OSSL_PKEY_PARAM_RSA_E, &e))
        goto done;

    bn = e;

    /* determine required size */
    {
        int num_bytes = BN_num_bytes(bn);

        if (num_bytes <= 0)
            goto done;

        required_size = (size_t)num_bytes;
    }

    /* fail if buffer too small */
    if (max_buffer_size < required_size)
        goto done;

    /* copy key bytes to caller buffer */
    if (!BN_bn2bin(bn, buffer))
        goto done;

    ret = required_size;

done:
    if (e)
        BN_free(e);
    if (n)
        BN_free(n);

    return ret;
}

ssize_t key_get_modulus(
    public_rsa_key_t* key,
    uint8_t* buffer,
    size_t max_buffer_size)
{
    ssize_t ret = -1;
    size_t required_size = 0;
    const BIGNUM* bn = NULL;
    BIGNUM* e = NULL;
    BIGNUM* n = NULL;

    if (!key || !buffer|| !max_buffer_size)
        goto done;

    if (!EVP_PKEY_get_bn_param((EVP_PKEY*)key, OSSL_PKEY_PARAM_RSA_N, &n))
        goto done;

    if (!EVP_PKEY_get_bn_param((EVP_PKEY*)key, OSSL_PKEY_PARAM_RSA_E, &e))
        goto done;

    bn = n;

    /* determine required size */
    {
        int num_bytes = BN_num_bytes(bn);

        if (num_bytes <= 0)
            goto done;

        required_size = (size_t)num_bytes;
    }

    /* fail if buffer too small */
    if (max_buffer_size < required_size)
        goto done;

    /* copy key bytes to caller buffer */
    if (!BN_bn2bin(bn, buffer))
        goto done;

    ret = required_size;

done:
    if (e)
        BN_free(e);
    if (n)
        BN_free(n);

    return ret;
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
        goto done;

    /* Initialize openssl */
    _initialize();

    /* Create signing context */
    if (!(ctx = EVP_PKEY_CTX_new((EVP_PKEY*)key, NULL)))
        goto done;

    /* Initialize the signing context */
    if (EVP_PKEY_verify_init(ctx) <= 0)
        goto done;

    /* Set the MD type for the signing operation */
    if (EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) <= 0)
        goto done;

    /* Verify the signature */
    if (EVP_PKEY_verify(
        ctx,
        signature,
        signature_size,
        hash->data,
        sizeof(sha256_t)) <= 0)
    {
        goto done;
    }

    ret = 0;

done:

    if (ctx)
        EVP_PKEY_CTX_free(ctx);

    return ret;
}

void free_private_rsa_key(private_rsa_key_t* key)
{
    if (key)
        EVP_PKEY_free((EVP_PKEY*)key);
}

void free_public_rsa_key(public_rsa_key_t* key)
{
    if (key)
        EVP_PKEY_free((EVP_PKEY*)key);
}
