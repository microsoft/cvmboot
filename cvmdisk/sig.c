// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "sig.h"
#include <stdio.h>
#include <unistd.h>
#include <utils/hexstr.h>
#include <utils/strings.h>
#include <common/err.h>
#include <common/file.h>
#include <common/key.h>
#include <common/exec.h>
#include "eraise.h"
#include "err.h"
#include "colors.h"

void sig_dump(const sig_t* p)
{
    const char* color = colors_cyan;
    const char* reset = colors_reset;
    printf("%smagic%s=%016lx\n", color, reset, p->magic);
    printf("%sversion%s=%lu\n", color, reset, p->version);
    printf("%sdigest%s=", color, reset);
    hexstr_dump(p->digest, sizeof(p->digest));
    printf("%ssigner%s=", color, reset);
    hexstr_dump(p->signer, sizeof(p->signer));
    printf("%ssignature%s=", color, reset);
    hexstr_dump(p->signature, p->signature_size);
    printf("%ssignature_size%s=%lu\n", color, reset, p->signature_size);
    printf("%sexponent%s=", color, reset);
    hexstr_dump(p->exponent, p->exponent_size);
    printf("%sexponent_size%s=%lu\n", color, reset, p->exponent_size);
    printf("%smodulus%s=", color, reset);
    hexstr_dump(p->modulus, p->modulus_size);
    printf("%smodulus_size%s=%lu\n", color, reset, p->modulus_size);
}

void sig_dump_signer(const sig_t* p)
{
    const char* color = colors_cyan;
    const char* reset = colors_reset;
    printf("%ssigner%s=", color, reset);
    hexstr_dump(p->signer, sizeof(p->signer));
}

int sig_create(
    const void* data,
    size_t size,
    const char* signtool_path,
    sig_t* sig)
{
    void* pubkey_data = NULL;
    size_t pubkey_size = 0;
    void* signature_data = NULL;
    size_t signature_size = 0;
    void* signerpubkeyhash_data = NULL;
    size_t signerpubkeyhash_size = 0;
    uint8_t modulus[SIG_MAX_MODULUS_SIZE];
    size_t modulus_size = sizeof(modulus);
    uint8_t exponent[SIG_MAX_EXPONENT_SIZE];
    size_t exponent_size = sizeof(exponent);
    sha256_t signer_hash;
    ssize_t r;
    public_rsa_key_t* pubkey = NULL;
    char tmpdir[] = "/tmp/cvmdisk_XXXXXX";
    sha256_t digest = SHA256_INITIALIZER;
    buf_t buf = BUF_INITIALIZER;
    char filename[PATH_MAX];
    char filename_sig[PATH_MAX];
    char filename_signerpubkeyhash[PATH_MAX];
    char filename_pub[PATH_MAX];

    memset(&signer_hash, 0, sizeof(signer_hash));
    memset(&modulus, 0, sizeof(modulus));
    memset(&exponent, 0, sizeof(exponent));

    if (!signtool_path)
        ERR("unexpected: null signtool_path parameter");

    /* compute the digest */
    sha256_compute(&digest, data, size);

    /* create a temporary directory */
    if (!mkdtemp(tmpdir))
        ERR("failed to create temporary directory");

    /* invoke the signing-tool */
    {
        strlcpy2(filename, tmpdir, "/filename", PATH_MAX);
        strlcpy2(filename_sig, tmpdir, "/filename.sig", PATH_MAX);
        strlcpy2(filename_signerpubkeyhash, tmpdir,
            "/filename.signerpubkeyhash", PATH_MAX);
        strlcpy2(filename_pub, tmpdir, "/filename.pub", PATH_MAX);

        if (write_file(filename, data, size) < 0)
            ERR("failed to create file: %s", filename);

        execf(&buf, "%s %s", signtool_path, filename);
    }

    /* load the signature file */
    if (load_file(filename_sig, &signature_data, &signature_size) != 0)
        ERR("failed to load signature: %s", filename_sig);

    /* load the signer hash file */
    if (load_file(filename_signerpubkeyhash,
        &signerpubkeyhash_data, &signerpubkeyhash_size) == 0)
    {
        printf("Found: %s\n", filename_signerpubkeyhash);
    }
    else
    {
        printf("Not found: %s\n", filename_signerpubkeyhash);
    }

    /* load the public key into memory */
    if (load_file(filename_pub, &pubkey_data, &pubkey_size) != 0)
        ERR("failed to load public key: %s", filename_pub);

    /* create the public RSA key */
    if (read_public_rsa_key(&pubkey, pubkey_data, pubkey_size + 1) < 0)
        ERR("failed to read public key: %s", filename_pub);

    /* remove temporary directory */
    unlink(filename);
    unlink(filename_sig);
    unlink(filename_signerpubkeyhash);
    unlink(filename_pub);
    rmdir(tmpdir);

    /* get the exponent of the key */
    {
        if ((r = key_get_exponent(pubkey, exponent, exponent_size)) < 0)
            ERR("failed to get modulus from public key: %s", filename_pub);

        exponent_size = r;

        if (exponent_size > SIG_MAX_EXPONENT_SIZE)
            ERR("modulus of key exceeds size of sig_t.modulus[]");
    }

    /* get the modulus of the key */
    {
        if ((r = key_get_modulus(pubkey, modulus, modulus_size)) < 0)
            ERR("failed to get modulus from public key: %s", filename_pub);

        modulus_size = r;

        if (modulus_size > SIG_MAX_MODULUS_SIZE)
            ERR("modulus of key exceeds size of sig_t.modulus[]");
    }

    /* compute the hash of the public key: SHA256(modulus||exponent) */
    {
        uint8_t buf[SIG_MAX_MODULUS_SIZE+SIG_MAX_EXPONENT_SIZE];
        const size_t buf_size = modulus_size + exponent_size;
        memcpy(buf, modulus, modulus_size);
        memcpy(&buf[modulus_size], exponent, exponent_size);
        sha256_compute(&signer_hash, buf, buf_size);
    }

    /* cross check against filename.signerpubkeyhash file (if any) */
    if (signerpubkeyhash_data && signerpubkeyhash_size)
    {
        if (signerpubkeyhash_size != SHA256_SIZE)
            ERR("file is wrong size: %s", filename_signerpubkeyhash);

        if (memcmp(signerpubkeyhash_data, &signer_hash, SHA256_SIZE) != 0)
            ERR("signer cross-check failed: %s", filename_signerpubkeyhash);

        printf("Signer cross-check okay: %s\n", filename_signerpubkeyhash);
    }

    /* Initialize the FSSIG structure */
    {
        memset(sig, 0, sizeof(sig_t));
        sig->magic = SIG_MAGIC;
        sig->version = SIG_VERSION;;
        memcpy(&sig->digest, &digest, sizeof(sig->digest));

        memcpy(&sig->signer, signer_hash.data, sizeof(sig->signer));

        if (signature_size > sizeof(sig->signature))
        {
            ERR("expected: signatured is too big: %zu > %zu",
                signature_size, sizeof(sig->signature));
        }

        memset(&sig->signature, 0, SIG_MAX_SIGNATURE_SIZE);
        memcpy(&sig->signature, signature_data, signature_size);
        sig->signature_size = signature_size;

        memset(&sig->exponent, 0, SIG_MAX_EXPONENT_SIZE);
        memcpy(&sig->exponent, exponent, exponent_size);
        sig->exponent_size = exponent_size;

        memset(&sig->modulus, 0, SIG_MAX_MODULUS_SIZE);
        memcpy(&sig->modulus, modulus, modulus_size);
        sig->modulus_size = modulus_size;
    }

    /* Verify the key */
    if (rsa_verify(
        pubkey,
        &digest,
        sig->signature,
        sig->signature_size) != 0)
    {
        ERR("failed to verify signature");
    }

    /* Check whether key created from sig struct can verify signature */
    {
        public_rsa_key_t* pubkey2 = NULL;

        if (create_rsa_key_from_exponent_and_modulus(
            &pubkey2,
            sig->exponent,
            sig->exponent_size,
            sig->modulus,
            sig->modulus_size) < 0)
        {
            ERR("failed to create key from signature");
        }

        /* Verify the key */
        if (rsa_verify(
            pubkey2,
            &digest,
            sig->signature,
            sig->signature_size) != 0)
        {
            ERR("failed to verify signature created from sig struct");
        }

        free_public_rsa_key(pubkey2);
    }

    free(pubkey_data);
    free_public_rsa_key(pubkey);
    free(signature_data);
    free(signerpubkeyhash_data);
    buf_release(&buf);

    return 0;
}
