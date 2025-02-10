// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/sha256.h>
#include <utils/strings.h>
#include <utils/sig.h>
#include <utils/key.h>
#include <utils/allocator.h>
#include <common/key.h>
#include <common/err.h>
#include <common/file.h>
#include <common/buf.h>
#include <common/exec.h>
#include <common/getoption.h>
#include <common/sudo.h>
#include "timestamp.h"

__attribute__((__used__)) static const char _timestamp[] = TIMESTAMP;

allocator_t __allocator = { malloc, free };

static const char* _basename(const char* path)
{
    char* p;

    if ((p = strrchr(path, '/')))
        return p + 1;

    return path;
}

static void _get_cvmsign_homedir(char path[PATH_MAX], bool check)
{
    char home[PATH_MAX];
    struct stat statbuf;

    if (sudo_get_home_dir(home) < 0)
        ERR("failed to resolve user's home directory");

    strlcpy(path, home, PATH_MAX);
    strlcat(path, "/.cvmsign", PATH_MAX);

    if (check)
    {
        if (stat(path, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
        {
            ERR("the cvmsign home directory was not found; "
                "consider running cvmsign-init to create it: %s", path);
        }
    }
}

static void _get_private_key_path(const char* homedir, char path[PATH_MAX])
{
    strlcpy(path, homedir, PATH_MAX);
    strlcat(path, "/private.pem", PATH_MAX);

    if (access(path, R_OK) < 0)
        ERR("private key does not exist: %s", path);
}

static void _get_public_key_path(const char* homedir, char path[PATH_MAX])
{
    strlcpy(path, homedir, PATH_MAX);
    strlcat(path, "/public.pem", PATH_MAX);

    if (access(path, R_OK) < 0)
        ERR("public key does not exist: %s", path);
}

static void _genkeys(const char* privkey, const char* pubkey)
{
    buf_t buf = BUF_INITIALIZER;

    execf(&buf, "openssl genrsa -out %s", privkey);
    execf(&buf,
        "openssl rsa -in %s -pubout -out %s 2>/dev/null", privkey, pubkey);

    if (access(privkey, R_OK) < 0)
        ERR("Failed to create %s", privkey);

    if (access(pubkey, R_OK) < 0)
        ERR("Failed to create %s", pubkey);

    buf_release(&buf);
}

int cvmsign_main(int argc, const char* argv[])
{
    sha256_t digest = SHA256_INITIALIZER;
    const char* filename;
    void* filename_data = NULL;
    size_t filename_size = 0;
    char home[PATH_MAX];
    char homedir[PATH_MAX];
    char privkey_path[PATH_MAX];
    char pubkey_path[PATH_MAX];
    void* privkey_data = NULL;
    size_t privkey_size = 0;
    void* pubkey_data = NULL;
    size_t pubkey_size = 0;
    private_rsa_key_t* privkey = NULL;
    public_rsa_key_t* pubkey = NULL;
    uint8_t signature[SIG_MAX_SIGNATURE_SIZE];
    size_t signature_size = sizeof(signature);
    uint8_t modulus[SIG_MAX_MODULUS_SIZE];
    size_t modulus_size = sizeof(modulus);
    uint8_t exponent[SIG_MAX_EXPONENT_SIZE];
    size_t exponent_size = sizeof(exponent);
    ssize_t r;
    sha256_t signer_hash = SHA256_INITIALIZER;

    if (sudo_get_home_dir(home) < 0)
        ERR("unexpected: failed to resolve user home directory");

    err_set_arg0(argv[0]);

    /* check the usage */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <file-name>\n", argv[0]);
        exit(1);
    }

    /* get cvmsign home directory and key paths */
    _get_cvmsign_homedir(homedir, true);
    _get_private_key_path(homedir, privkey_path);
    _get_public_key_path(homedir, pubkey_path);

    /* get the filename of the file being signed */
    filename = argv[1];

    /* load the file being signed into memory */
    if (load_file(filename, &filename_data, &filename_size) != 0)
        ERR("failed read file: %s", filename);

    /* Get the SHA-256 of the file */
    sha256_compute(&digest, filename_data, filename_size);

    /* load the private key into memory */
    {
        if (load_file(privkey_path, &privkey_data, &privkey_size) != 0)
            ERR("failed to load private key: %s", privkey_path);

        if (read_private_rsa_key(&privkey, privkey_data, privkey_size + 1) < 0)
            ERR("failed to read private key: %s", privkey_path);
    }

    /* load the public key into memory */
    {
        if (load_file(pubkey_path, &pubkey_data, &pubkey_size) != 0)
            ERR("failed to load public key: %s", pubkey_path);

        if (read_public_rsa_key(&pubkey, pubkey_data, pubkey_size + 1) < 0)
            ERR("invalid public key: %s", pubkey_path);
    }

    /* get the exponent of the key */
    {
        if ((r = key_get_exponent(pubkey, exponent, exponent_size)) < 0)
            ERR("failed to get modulus from public key: %s", pubkey_path);

        exponent_size = r;

        if (exponent_size > SIG_MAX_EXPONENT_SIZE)
            ERR("modulus of key exceeds size of sig_t.modulus[]");
    }

    /* get the modulus of the key */
    {
        if ((r = key_get_modulus(pubkey, modulus, modulus_size)) < 0)
            ERR("failed to get modulus from public key: %s", pubkey_path);

        modulus_size = r;

        if (modulus_size > SIG_MAX_MODULUS_SIZE)
            ERR("modulus of key exceeds size of sig_t.modulus[]");
    }

    /* compute the SHA256(n||e) */
    {
        uint8_t buf[SIG_MAX_MODULUS_SIZE+SIG_MAX_EXPONENT_SIZE];
        const size_t buf_size = modulus_size + exponent_size;
        memcpy(buf, modulus, modulus_size);
        memcpy(&buf[modulus_size], exponent, exponent_size);
        sha256_compute(&signer_hash, buf, buf_size);
    }

    /* sign the digest */
    {
        ssize_t r;

        if ((r = rsa_sign(privkey, &digest, signature, signature_size)) < 0)
        {
            ERR("signing operation failed: %s", privkey_path);
        }

        signature_size = r;

        if (signature_size > SIG_MAX_SIGNATURE_SIZE)
            ERR("unexpected: signature is too big: %zu\n", signature_size);
    }

    /* write the signature file */
    {
        char path[PATH_MAX];

        strlcpy(path, filename, sizeof(path));
        strlcat(path, ".sig", sizeof(path));

        if (write_file(path, signature, signature_size) < 0)
            ERR("failed to write file: %s", path);

        printf("%s: Created %s\n", argv[0], path);
    }

    /* write the signer hash file */
    {
        char path[PATH_MAX];

        strlcpy(path, filename, sizeof(path));
        strlcat(path, ".signerpubkeyhash", sizeof(path));

        if (write_file(path, &signer_hash, sizeof(signer_hash)) < 0)
            ERR("failed to write file: %s", path);

        printf("%s: Created %s\n", argv[0], path);
    }

    /* write the public PEM file */
    {
        char path[PATH_MAX];

        strlcpy(path, filename, sizeof(path));
        strlcat(path, ".pub", sizeof(path));

        if (write_file(path, pubkey_data, pubkey_size) < 0)
            ERR("failed to write file: %s", path);

        printf("%s: Created %s\n", argv[0], path);
    }

    free(privkey_data);
    free(filename_data);
    free(pubkey_data);
    free_public_rsa_key(pubkey);

    return 0;
}

int cvmsign_init_main(int argc, const char* argv[])
{
    char homedir[PATH_MAX];
    struct stat statbuf;
    char privkey_path[PATH_MAX];
    char pubkey_path[PATH_MAX];
    bool force = false;
    err_t err;

    err_set_arg0(argv[0]);

    if (getoption(&argc, argv, "--force", NULL, &err) == 0)
        force = true;
    else if (getoption(&argc, argv, "-f", NULL, &err) == 0)
        force = true;

    /* check the usage */
    if (argc != 1)
    {
        fprintf(stderr, "Usage: %s [--force|-f]\n", argv[0]);
        exit(1);
    }

    /* get cvmsign home directory: $HOME/.cvmsign */
    _get_cvmsign_homedir(homedir, false);

    /* create the directory if it does not already exist */
    if (stat(homedir, &statbuf) != 0)
    {
        /* create the directory */
        if (mkdir(homedir, 0755) < 0)
            ERR("failed to create directory: %s", homedir);
    }
    else
    {
        if (!force)
            printf("%s: already exists: %s\n", argv[0], homedir);
    }

    /* verify that it is a directory */
    if (stat(homedir, &statbuf) != 0 || !S_ISDIR(statbuf.st_mode))
        ERR("not a directory: %s\n", homedir);

    /* Form the public key path */
    strlcpy(privkey_path, homedir, PATH_MAX);
    strlcat(privkey_path, "/private.pem", PATH_MAX);

    /* Form the private key path */
    strlcpy(pubkey_path, homedir, PATH_MAX);
    strlcat(pubkey_path, "/public.pem", PATH_MAX);

    /* Only create the keys if they do not already exist */
    if (access(privkey_path, F_OK) == 0 && access(pubkey_path, F_OK) == 0 &&
        !force)
    {
        printf("%s: already exists: %s\n", argv[0], privkey_path);
        printf("%s: already exists: %s\n", argv[0], pubkey_path);
    }
    else
    {
        _genkeys(privkey_path, pubkey_path);
        printf("%s: Created %s\n", argv[0], privkey_path);
        printf("%s: Created %s\n", argv[0], pubkey_path);
    }

    return 0;
}

int cvmsign_verify_main(int argc, const char* argv[])
{
    const char* filename_path;
    const char* signature_path;
    const char* pubkey_path;
    void* filename_data = NULL;
    size_t filename_size = 0;
    void* signature_data = NULL;
    size_t signature_size = 0;
    void* pubkey_data = NULL;
    size_t pubkey_size = 0;
    public_rsa_key_t* pubkey = NULL;
    sha256_t digest = SHA256_INITIALIZER;

    err_set_arg0(argv[0]);

    /* check the usage */
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <file-name> <signature> <public-key>\n",
            argv[0]);
        exit(1);
    }

    /* get args */
    filename_path = argv[1];
    signature_path = argv[2];
    pubkey_path = argv[3];

    /* load file into memory */
    if (load_file(filename_path, &filename_data, &filename_size) < 0)
        ERR("failed to read file: %s", filename_path);

    /* load signature into memory */
    if (load_file(signature_path, &signature_data, &signature_size) < 0)
        ERR("failed to read file: %s", signature_path);

    /* fail if signature it too big */
    if (signature_size > SIG_MAX_SIGNATURE_SIZE)
    {
        ERR("signature is too big: %zu > %u\n", signature_size,
            SIG_MAX_SIGNATURE_SIZE);
    }

    /* load public key into memory */
    if (load_file(pubkey_path, &pubkey_data, &pubkey_size) < 0)
        ERR("failed to read file: %s", pubkey_path);

    /* create the public key */
    if (read_public_rsa_key(&pubkey, pubkey_data, pubkey_size + 1) < 0)
        ERR("invalid public key: %s", pubkey_path);

    /* Compute the SHA-256 digest */
    sha256_compute(&digest, filename_data, filename_size);

    /* Verify the signature */
    if (rsa_verify(pubkey, &digest, signature_data, signature_size) != 0)
        ERR("verification failed");

    printf("%s: verification okay\n", argv[0]);

    free(filename_data);
    free(signature_data);
    free(pubkey_data);
    free_public_rsa_key(pubkey);

    return 0;
}

int main(int argc, const char* argv[])
{
    const char* basename = _basename(argv[0]);

    if (strcmp(basename, "cvmsign") == 0)
    {
        return cvmsign_main(argc, argv);
    }
    else if (strcmp(basename, "cvmsign-init") == 0)
    {
        return cvmsign_init_main(argc, argv);
    }
    else if (strcmp(basename, "cvmsign-verify") == 0)
    {
        return cvmsign_verify_main(argc, argv);
    }
    else
    {
        fprintf(stderr, "%s: no such command name: %s\n", argv[0], basename);
        exit(1);
    }
}
