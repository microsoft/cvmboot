// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <openssl/evp.h>
#include <utils/sha256.h>
#include <string.h>
#include "sig.h"
#include "key.h"
#include "console.h"

static int sig_to_bootloader_sig(bootloader_sig_t* bootloader_sig, const sig_t* disk_sig)
{
    sha256_t computed_signer;

    if (!bootloader_sig || !disk_sig)
        return -1;

    /* Clear the bootloader signature structure */
    memset(bootloader_sig, 0, sizeof(bootloader_sig_t));

    /* Copy fields from disk signature struct (only fields used in signature
     * verification) */
    memcpy(bootloader_sig->digest, disk_sig->digest, SIG_DIGEST_SIZE);
    memcpy(bootloader_sig->signature, disk_sig->signature, SIG_MAX_SIGNATURE_SIZE);
    bootloader_sig->signature_size = disk_sig->signature_size;
    memcpy(bootloader_sig->exponent, disk_sig->exponent, SIG_MAX_EXPONENT_SIZE);
    bootloader_sig->exponent_size = disk_sig->exponent_size;
    memcpy(bootloader_sig->modulus, disk_sig->modulus, SIG_MAX_MODULUS_SIZE);
    bootloader_sig->modulus_size = disk_sig->modulus_size;

    /* Compute the signer field from SHA256(modulus||exponent) to ensure
     * PCR measurements reflect the actual cryptographic identity of the signer
     * based on the key material */
    sha256_compute2(
        &computed_signer,
        bootloader_sig->modulus,
        bootloader_sig->modulus_size,
        bootloader_sig->exponent,
        bootloader_sig->exponent_size
        );

    memcpy(bootloader_sig->signer, computed_signer.data, SIG_SIGNER_SIZE);

    return 0;
}

static int bootloader_sig_verify(const bootloader_sig_t* sig)
{
    int ret = -1;
    public_rsa_key_t* pubkey = NULL;

    /* Create the RSA key from the exponent and modulus */
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

    /* Verify the signature */
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

int sig_convert_and_verify(const sig_t* disk_sig, bootloader_sig_t* bootloader_sig)
{
    /* Convert disk signature to bootloader signature with computed signer */
    if (sig_to_bootloader_sig(bootloader_sig, disk_sig) < 0)
    {
        Print(L"Failed to convert signature to bootloader format");
        pause(NULL);
        return -1;
    }
    
    /* Verify using the bootloader signature */
    return bootloader_sig_verify(bootloader_sig);
}
