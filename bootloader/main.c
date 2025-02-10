// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <eficon.h>
#include <utils/sig.h>
#include <utils/hexstr.h>
#include <utils/events.h>
#include <utils/paths.h>
#include <utils/cpio.h>
#include <utils/strings.h>
#include "tpm2.h"
#include "key.h"
#include "tcg2.h"
#include "cpio.h"
#include "console.h"
#include "globals.h"
#include "system.h"
#include "efifile.h"
#include "console.h"
#include "sig.h"
#include "events.h"
#include "kernel.h"
#include "sleep.h"
#include "efivar.h"
#include "console.h"
#include "timestamp.h"
#include "conf.h"

__attribute__((__used__)) static const char _timestamp[] = TIMESTAMP;

extern unsigned char logo[];

const char VERSION[] = "cvmboot version 0.1";

void print_splash_screen(void)
{
    efi_clear_screen();
    efi_set_colors(EFI_LIGHTGREEN, EFI_BLACK);
    Print(L"%a\n", logo);
    efi_set_colors(EFI_WHITE, EFI_BLACK);
    Print(L"%a\n", VERSION);
    efi_set_colors(EFI_LIGHTGRAY, EFI_BLACK);
    Print(L"\n");
}

static int _set_roothash_efi_variable(const sha256_t* hash)
{
    int ret = -1;
    /* 08b5e462-25eb-42c0-a7d1-e9f78c3c7e09 */
    static const EFI_GUID guid =
        {0x08b5e462,0x25eb,0x42c0,{0xa7,0xd1,0xe9,0xf7,0x8c,0x3c,0x7e,0x09}};
    sha256_string_t str;

    sha256_format(&str, hash);

    if (set_efi_var(L"roothash", &guid, str.buf, strlen(str.buf) + 1) < 0)
        goto done;

    ret = 0;

done:
    return ret;
}

EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table)
{
    const BOOLEAN trace = FALSE;
    err_t err;
    sig_t sig;
    CHAR16 cpio_path[PATH_MAX];
    void* cpio_data = NULL;
    UINTN cpio_size = 0;
    conf_t conf;

    InitializeLib(image_handle, system_table);

    globals.image_handle = image_handle;
    globals.system_table = system_table;

    /* Create the TCG2 protocol (needed to measure PCRs) */
    if (!(globals.tcg2 = TCG2_GetProtocol(&err)))
    {
        Print(L"Failed to get the TCG2 protocol");
        system_reset();
    }

    /* Print the splash screen */
    {
        print_splash_screen();

        if (trace)
            pause(NULL);

        Sleep(1);
    }

    /* Initialize the crypto engine */
    crypto_initialize();

    /* Get path of cvmboot.cpio */
    paths_getw(cpio_path, FILENAME_CVMBOOT_CPIO);

    /* Load cvmboot.cpio */
    if (efi_file_load(
        image_handle,
        cpio_path,
        &cpio_data,
        &cpio_size) != EFI_SUCCESS)
    {
        Print(L"Failed to load %s\n", cpio_path);
        pause(NULL);
        system_reset();
    }

    /* Load and verify the signature from cvmboot.cpio.sig */
    {
        CHAR16 path[PATH_MAX];
        void* data = NULL;
        UINTN size;

        paths_getw(path, FILENAME_CVMBOOT_CPIO_SIG);

        /* Load cvmboot.cpio.sig */
        if (efi_file_load(image_handle, path, &data, &size) != EFI_SUCCESS)
        {
            Print(L"Failed to load %s\n", cpio_path);
            pause(NULL);
            system_reset();
        }

        /* Verify the signature size */
        if (size != sizeof(sig_t))
        {
            Print(L"Bad signature size: %s\n", path);
            pause(NULL);
            system_reset();
        }

        memcpy(&sig, data, sizeof(sig_t));
        FreePool(data);

        /* Check the signature magic number */
        if (sig.magic != SIG_MAGIC)
        {
            Print(L"Bad magic number: %s\n", path);
            pause(NULL);
            system_reset();
        }

        /* Verify the signature against the signature structure */
        if (sig_verify(&sig) < 0)
        {
            Print(L"Failed to verify signature against exponent/modulus");
            pause(NULL);
            system_reset();
        }

        /* Check the digest of the CPIO against the signature */
        {
            sha256_t hash;

            memset(&hash, 0, sizeof(hash));
            sha256_compute(&hash, cpio_data, cpio_size);

            if (memcmp(&hash, &sig.digest, sizeof(sha256_t)) != 0)
            {
                Print(L"Invalid cvmboot.cpio hash: %s", cpio_path);
                pause(NULL);
                system_reset();
            }
        }
    }

    /* Load the configuration file */
    if (conf_load(cpio_data, cpio_size, &conf, &err) < 0)
    {
        Print(L"failed to load configuration file: %a\n", err.buf);
        system_reset();
    }

    /* Set the "roothash-<partition-number>" EFI variables */
    if (_set_roothash_efi_variable(&conf.roothash) < 0)
    {
        Print(L"Failed to set roothash EFI variable\n");
        system_reset();
    }

    /* Load and measure identity/events files */
    {
        char signer[SHA256_STRING_SIZE] = "";
        void* data = NULL;
        size_t size;

        /* Convert the sig.signer to string */
        hexstr_format(
            signer,
            sizeof(signer),
            sig.signer,
            sizeof(sig.signer));

        if (cpio_load_file(
            cpio_data, cpio_size, FILENAME_EVENTS, &data, &size) == 0)
        {
            err_t err;
            unsigned int error_line;

            /* Measure everything in the events file */
            if (parse_events_file(
                data,
                size,
                signer,
                process_events_callback,
                NULL, /* callback data */
                &error_line,
                &err) != 0)
            {
                Print(L"failed to parse events file: %a\n", err.buf);
                pause(NULL);
                system_reset();
            }
        }
        else
        {
            if (hash_log_extend_binary_event(11, signer) < 0)
            {
                Print(L"failed hash-log-extend of signer: %a\n", signer);
                pause(NULL);
                system_reset();
            }
        }

        if (data)
            FreePool(data);
    }

    /* Start the Linux kernel */
    {
        const void* kernel_data = NULL;
        size_t kernel_size;
        const void* initrd_data = NULL;
        size_t initrd_size;

        // Load vmlinuz
        if (cpio_load_file_direct_by_name(cpio_data, cpio_size,
            conf.kernel, &kernel_data, &kernel_size) != 0)
        {
            Print(L"Failed to load kernel\n");
            pause(NULL);
            system_reset();
        }

        // Load initrd.img
        if (cpio_load_file_direct_by_name(cpio_data, cpio_size,
            conf.initrd, &initrd_data, &initrd_size) != 0)
        {
            Print(L"Failed to load initrd\n");
            pause(NULL);
            system_reset();
        }

        // Start the Linux kernel
        if (start_kernel(
            kernel_data,
            kernel_size,
            initrd_data,
            initrd_size,
            conf.cmdline) != 0)
        {
            Print(L"*** Failed to start the Linux kernel\n");
            pause(NULL);
            system_reset();
        }
    }

    Print(L"Unexpected failure after trying to start the Linux kernel\n");
    pause(NULL);
    system_reset();

    return EFI_SUCCESS;
}
