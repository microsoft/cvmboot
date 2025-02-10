// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string.h>
#include <stdlib.h>
#include <utils/strings.h>
#include "globals.h"
#include "kernel.h"
#include "efifile.h"
#include "console.h"
#include "sleep.h"

#define SECTOR_SIZE ((UINTN)512)
#define MINIMUM_SUPPORTED_VERSION 0x020B

typedef struct boot_params boot_params_t;
typedef struct setup_header setup_header_t;

struct setup_header
{
#   define DEFAULT_SETUP_SECTS 4
#   define MAX_SETUP_SECTS 64
    UINT8 setup_sects;
    UINT16 root_flags;
    UINT32 syssize;
    UINT16 ram_size;
    UINT16 vid_mode;
    UINT16 root_dev;
#   define BOOT_FLAG 0xAA55
    UINT16 boot_flag;
    UINT16 jump;
#   define HEADER_MAGIC 0x53726448
    UINT32 header;
    UINT16 version;
    UINT32 realmode_swtch;
    UINT16 start_sys_seg;
    UINT16 kernel_version;
    UINT8 type_of_loader;
#   define LOADED_HIGH (1 << 0)
#   define KASLR_FLAG (1 << 1)
#   define QUIET_FLAG (1 << 5)
#   define KEEP_SEGMENTS (1 << 6)
#   define CAN_USE_HEAP (1 << 7)
    UINT8 loadflags;
    UINT16 setup_move_size;
    UINT32 code32_start;
    UINT32 ramdisk_image;
    UINT32 ramdisk_size;
    UINT32 bootsect_kludge;
    UINT16 heap_end_ptr;
    UINT8 ext_loader_ver;
    UINT8 ext_loader_type;
    UINT32 cmd_line_ptr;
    UINT32 initrd_addr_max;
    UINT32 kernel_alignment;
    UINT8 relocatable_kernel;
    UINT8 min_alignment;
    UINT16 xloadflags;
    UINT32 cmdline_size;
    UINT32 hardware_subarch;
    UINT64 hardware_subarch_data;
    UINT32 payload_offset;
    UINT32 payload_length;
    UINT64 setup_data;
#   define BZIMAGE_ADDRESS 0x100000
    UINT64 pref_address;
    UINT32 init_size;
    UINT32 handover_offset;
}
__attribute__((packed));

struct boot_params
{
    UINT8 __pad1[0x01F1];
#   define SETUP_OFFSET 0x01F1
    setup_header_t setup;
    /* Pad out to two sectors */
    UINT8 __pad2[1024 - sizeof(setup_header_t) - 0x01F1];
}
__attribute__((packed));

static UINTN _ByteToPages(UINTN bytes)
{
    const UINTN pageSize = 4096;
    return (bytes + pageSize - 1) / pageSize;
}

typedef void(*HandoverFunction)(
    EFI_HANDLE imageHandle,
    EFI_SYSTEM_TABLE *systemTable,
    boot_params_t *bootParams);

static EFI_STATUS _InvokeHandoverFunction(
    void* kernelData,
    UINTN kernelSize,
    boot_params_t* params,
    UINT32 handoverOffset)
{
    EFI_STATUS status = EFI_UNSUPPORTED;
    HandoverFunction func;

    /* Check parameters */
    if (!kernelData || !kernelSize || !params || !handoverOffset)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    /* Check whether offset is out of range */
    if (handoverOffset >= kernelSize)
    {
        status = EFI_UNSUPPORTED;
        goto done;
    }

    /* X86-64-Bit requires adding 512 bytes more (one sector) */
    func = (HandoverFunction)((char*)kernelData + handoverOffset + 512);

    /* Clear interrupts */
    asm volatile ("cli");

    /* Invoke the function (should not return) */
    (*func)(globals.image_handle, globals.system_table, params);

    status = EFI_SUCCESS;

done:
    return status;
}

static EFI_STATUS _AllocatePages(
    EFI_ALLOCATE_TYPE type,
    EFI_PHYSICAL_ADDRESS address,
    UINTN numPages,
    void** ptr)
{
    EFI_STATUS status = EFI_UNSUPPORTED;

    if (address == 0 || !ptr || numPages == 0)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    *ptr = NULL;

    if ((status = uefi_call_wrapper(
        BS->AllocatePages,
        4,
        type,
        EfiLoaderData,
        numPages,
        &address)) != EFI_SUCCESS)
    {
        goto done;
    }

    if (!address)
    {
        Print(L"AllocatePages() returned zero address\n");
        status = EFI_OUT_OF_RESOURCES;
        goto done;
    }

    *ptr = (void*)address;
    status = EFI_SUCCESS;

done:

    return status;
}

static EFI_STATUS _FreePages(
    void* ptr,
    UINTN numPages)
{
    EFI_STATUS status = EFI_UNSUPPORTED;

    if (!ptr || numPages == 0)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    if ((status = uefi_call_wrapper(
        BS->FreePages,
        2,
        (EFI_PHYSICAL_ADDRESS)ptr,
        numPages)) != EFI_SUCCESS)
    {
        goto done;
    }

done:
    return status;
}

static EFI_STATUS _LoadInitrd(
    const void* data,
    size_t size,
    void** initrdDataOut,
    UINTN* initrdSizeOut,
    UINT32* errorLine)
{
    EFI_STATUS status = EFI_UNSUPPORTED;
    void* initrdData = NULL; /* pointer to initrd pages */
    UINTN initrdSize = 0; /* aligned size of initrd */

    /* Check for null parameters */
    if (!size || !data || !initrdDataOut || !initrdSizeOut || !errorLine)
    {
        status = EFI_INVALID_PARAMETER;
        *errorLine = __LINE__;
        goto done;
    }

    /* Initialize output parameters */
    *initrdDataOut = NULL;
    *initrdSizeOut = 0;
    *errorLine = 0;

    /* Round size of the file to next multiple of 4 */
    initrdSize = (size + 3) / size * size;

    /* Allocate space to place this file into memory for the kernel */
    if ((status = _AllocatePages(
        AllocateMaxAddress,
        0x3fffffff,
        _ByteToPages(initrdSize),
        &initrdData)) != EFI_SUCCESS)
    {
        *errorLine = __LINE__;
        goto done;
    }

    /* Copy loaded file onto allocated memory */
    memcpy(initrdData, data, size);

    /* Zero fill any padding bytes at the end */
    if (initrdSize > size)
    {
        memset((unsigned char*)initrdData + size, 0, initrdSize - size);
    }

    /* Set output parameters */
    *initrdDataOut = initrdData;
    *initrdSizeOut = initrdSize;

    /* Success! */
    *errorLine = 0;
    status = EFI_SUCCESS;

done:

    if (status != EFI_SUCCESS)
    {
        if (initrdData)
            _FreePages(initrdData, _ByteToPages(initrdSize));
    }

    return status;
}

static EFI_STATUS _CheckSetupHeader(
    setup_header_t* header)
{
    EFI_STATUS status = EFI_UNSUPPORTED;

    /* Check parameters */
    if (!header)
    {
        goto done;
    }

    /* Check the boot flag magic number */
    if (header->boot_flag != BOOT_FLAG)
    {
        goto done;
    }

    /* Check whether too many setup sections */
    if (header->setup_sects > MAX_SETUP_SECTS)
    {
        goto done;
    }

    /* Reject old versions of kernel (< 2.11) */
    if (header->version < MINIMUM_SUPPORTED_VERSION)
    {
        goto done;
    }

    /* If kernel does not have an EFI handover offset */
    if (header->handover_offset == 0)
    {
        goto done;
    }

    status = EFI_SUCCESS;

done:
    return status;
}

static EFI_STATUS _LoadKernel(
    const void* data,
    size_t size,
    const char* linux_cmdline,
    UINT32* handoverOffset,
    void** kernelDataOut,
    UINTN* kernelSizeOut,
    boot_params_t **paramsDataOut,
    UINT32* errorLine)
{
    EFI_STATUS status = EFI_UNSUPPORTED;
    setup_header_t sh;
    UINTN numPages;
    boot_params_t* paramsData = NULL;
    const UINTN paramsSize = 16384;
    static char *cmdline = NULL;
    UINTN kernelStart;
    void* kernelData = NULL;
    UINTN kernelSize;

    /* Check parameters */
    if (!data || !size || !linux_cmdline || !handoverOffset ||
        !kernelDataOut || !kernelSizeOut || !paramsDataOut || !errorLine)
    {
        status = EFI_INVALID_PARAMETER;
        *errorLine = __LINE__;
        goto done;
    }

    /* If kernel is too small to even have leading params */
    if (size < paramsSize)
    {
        status = EFI_UNSUPPORTED;
        *errorLine = __LINE__;
        goto done;
    }

    /* Calculate the required number of pages */
    numPages = _ByteToPages(paramsSize);

    /* Allocate space for the parameters */
    if ((status = _AllocatePages(
        AllocateMaxAddress,
        0x3fffffff,
        numPages,
        (void**)&paramsData)) != EFI_SUCCESS)
    {
        *errorLine = __LINE__;
        goto done;
    }

    /* Zero-fill the memory */
    memset(paramsData, 0, paramsSize);

    /* Get the header */
    memcpy(&sh, (uint8_t*)data + SETUP_OFFSET, sizeof(sh));

    /* Check sanity of setup header */
    if ((status = _CheckSetupHeader(&sh)) != EFI_SUCCESS)
    {
        *errorLine = __LINE__;
        goto done;
    }

    /* Allocate space for the Linux command line */
    if ((status = _AllocatePages(
        AllocateMaxAddress,
        0x3fffffff,
        _ByteToPages(sh.cmdline_size + 1),
        (void**)&cmdline)) != EFI_SUCCESS)
    {
        *errorLine = __LINE__;
        goto done;
    }

    /* Copy the linux_cmdline to page memory */
    strlcat(cmdline, linux_cmdline, sh.cmdline_size + 1);

    /* Set the pointer to the command line */
    sh.cmd_line_ptr = (UINT32)(EFI_PHYSICAL_ADDRESS)cmdline;

    /* Set the handover offset output parameter */
    *handoverOffset = sh.handover_offset;

    /* Find the start of the image (after all the setup sectors) */
    kernelStart = (sh.setup_sects + 1) * SECTOR_SIZE;

    /* Find the size of the kernel proper (excluding setup sectors) */
    kernelSize = size - kernelStart;

    /* Allocate space for eventual uncompressed kernel (sh.init_size) */
    if ((status = _AllocatePages(
        AllocateAddress,
        sh.pref_address,
        _ByteToPages(sh.init_size),
        &kernelData)) != EFI_SUCCESS)
    {
        if ((status = _AllocatePages(
            AllocateMaxAddress,
            0x3fffffff,
            _ByteToPages(sh.init_size),
            &kernelData)) != EFI_SUCCESS)
        {
            *errorLine = __LINE__;
            goto done;
        }
    }

    /* Copy the kernel onto the allocated pages (excluding startup) */
    memcpy(
        kernelData,
        (unsigned char*)data + kernelStart,
        kernelSize);

    /* Set pointer to kernel */
    sh.code32_start = (UINT32)(EFI_PHYSICAL_ADDRESS)kernelData;

    /* Set the loader type (unknown boot loader) */
    sh.type_of_loader = 0xFF;

    /* Copy the leading bytes and header onto the params pages */
    memcpy(paramsData, data, SETUP_OFFSET);
    memcpy((unsigned char*)paramsData + SETUP_OFFSET, &sh, sizeof(sh));

    /* Set the output parameters */
    *kernelDataOut = kernelData;
    *kernelSizeOut = kernelSize;
    *paramsDataOut = paramsData;

    *errorLine = 0;
    status = EFI_SUCCESS;

done:

    if (status != EFI_SUCCESS)
    {
        if (paramsData)
            _FreePages(paramsData, _ByteToPages(paramsSize));

        if (cmdline)
            _FreePages(cmdline, _ByteToPages(sh.cmdline_size + 1));

        if (kernelData)
            _FreePages(kernelData, _ByteToPages(sh.init_size));

        *kernelDataOut = NULL;
        *kernelSizeOut = 0;
        *paramsDataOut = NULL;
    }

    return status;
}

EFI_STATUS start_kernel(
    const void* kernel_data,
    size_t kernel_size,
    const void* initrd_data,
    size_t initrd_size,
    const char* linux_cmdline)
{
    EFI_STATUS status = EFI_UNSUPPORTED;
    UINT32 handoverOffset = 0;
    void* kernelData = NULL;
    UINTN kernelSize = 0;
    boot_params_t *paramsData = NULL;
    void* initrdData = NULL;
    UINTN initrdSize = 0;
    UINT32 errorLine = 0;

    /* If the kernel and initrd paths are null, then fail now */
    if (!kernel_data || !kernel_size || !initrd_data || !initrd_size ||
        !linux_cmdline)
    {
        Print(L"Bad arguments: %a\n", __FUNCTION__);
        goto done;
    }

    /* Load the kernel */
    if ((status = _LoadKernel(
        kernel_data,
        kernel_size,
        linux_cmdline,
        &handoverOffset,
        &kernelData,
        &kernelSize,
        &paramsData,
        &errorLine)) != EFI_SUCCESS)
    {
        Print(L"Failed to load kernel: %d\n", errorLine);
        goto done;
    }

    /* Load the initial ramdisk */
    if ((status = _LoadInitrd(
        initrd_data,
        initrd_size,
        &initrdData,
        &initrdSize,
        &errorLine)) != EFI_SUCCESS)
    {
        Print(L"Failed to load initrd: %d\n", errorLine);
        goto done;
    }

    // Print(L"loaded initrd: {%s}\n", initrd_path);

    /* Update the params with the initrd info */
    paramsData->setup.ramdisk_size = initrdSize;
    paramsData->setup.ramdisk_image = (UINT32)(EFI_PHYSICAL_ADDRESS)initrdData;

    /* Print status message */
    efi_set_colors(EFI_LIGHTCYAN, EFI_BLACK);
    Print(L"Starting kernel...\n");
    efi_set_colors(EFI_LIGHTGRAY, EFI_BLACK);
    // Sleep(2);

    /* Jump to the EFI entry point in the kernel */
    if ((status = _InvokeHandoverFunction(
        kernelData,
        kernelSize,
        paramsData,
        handoverOffset)) != EFI_SUCCESS)
    {
        Print(L"Failed to invoke EFI handover function\n");
        goto done;
    }

    // Print(L"Returned from InvokeHandoverFunction()\n");

    /* Failure! Function above should never return */
    status = EFI_UNSUPPORTED;

done:
    return status;
}
