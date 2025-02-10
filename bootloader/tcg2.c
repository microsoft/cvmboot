// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <efi.h>
#include <efilib.h>
#include <string.h>
#include <utils/status.h>
#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include "tcg2.h"

const CHAR16* TCG2_StatusToStr(EFI_STATUS status)
{
    typedef struct _Pair
    {
        EFI_STATUS status;
        const CHAR16 *string;
    }
    Pair;

    static const Pair arr[] =
    {
        { EFI_SUCCESS, L"EFI_SUCCESS" },
        { EFI_LOAD_ERROR, L"EFI_LOAD_ERROR" },
        { EFI_INVALID_PARAMETER, L"EFI_INVALID_PARAMETER" },
        { EFI_UNSUPPORTED, L"EFI_UNSUPPORTED" },
        { EFI_BAD_BUFFER_SIZE, L"EFI_BAD_BUFFER_SIZE" },
        { EFI_BUFFER_TOO_SMALL, L"EFI_BUFFER_TOO_SMALL" },
        { EFI_NOT_READY, L"EFI_NOT_READY" },
        { EFI_DEVICE_ERROR, L"EFI_DEVICE_ERROR" },
        { EFI_WRITE_PROTECTED, L"EFI_WRITE_PROTECTED" },
        { EFI_OUT_OF_RESOURCES, L"EFI_OUT_OF_RESOURCES" },
        { EFI_VOLUME_CORRUPTED, L"EFI_VOLUME_CORRUPTED" },
        { EFI_VOLUME_FULL, L"EFI_VOLUME_FULL" },
        { EFI_NO_MEDIA, L"EFI_NO_MEDIA" },
        { EFI_MEDIA_CHANGED, L"EFI_MEDIA_CHANGED" },
        { EFI_NOT_FOUND, L"EFI_NOT_FOUND" },
        { EFI_ACCESS_DENIED, L"EFI_ACCESS_DENIED" },
        { EFI_NO_RESPONSE, L"EFI_NO_RESPONSE" },
        { EFI_NO_MAPPING, L"EFI_NO_MAPPING" },
        { EFI_TIMEOUT, L"EFI_TIMEOUT" },
        { EFI_NOT_STARTED, L"EFI_NOT_STARTED" },
        { EFI_ALREADY_STARTED, L"EFI_ALREADY_STARTED" },
        { EFI_ABORTED, L"EFI_ABORTED" },
        { EFI_ICMP_ERROR, L"EFI_ICMP_ERROR" },
        { EFI_TFTP_ERROR, L"EFI_TFTP_ERROR" },
        { EFI_PROTOCOL_ERROR, L"EFI_PROTOCOL_ERROR" },
        { EFI_INCOMPATIBLE_VERSION, L"EFI_INCOMPATIBLE_VERSION" },
        { EFI_SECURITY_VIOLATION, L"EFI_SECURITY_VIOLATION" },
        { EFI_CRC_ERROR, L"EFI_CRC_ERROR" },
        { EFI_END_OF_MEDIA, L"EFI_END_OF_MEDIA" },
        { EFI_END_OF_FILE, L"EFI_END_OF_FILE" },
        { EFI_INVALID_LANGUAGE, L"EFI_INVALID_LANGUAGE" },
        { EFI_COMPROMISED_DATA, L"EFI_COMPROMISED_DATA" },
    };
    UINT32 n = sizeof(arr) / sizeof(arr[0]);
    UINT32 i;

    for (i = 0; i < n; i++)
    {
        if (arr[i].status == status)
            return arr[i].string;
    }

    return L"UNKNOWN";
}

void DumpTCG2Capability(
    const EFI_TCG2_BOOT_SERVICE_CAPABILITY* capability)
{
    printf("struct EFI_TCG2_BOOT_SERVICE_CAPABILITY\n");
    printf("{\n");

    printf("    Size{%d}\n", capability->Size);

    printf("    StructureVersion.Major{%d}\n",
        capability->StructureVersion.Major);

    printf("    StructureVersion.Minor{%d}\n",
        capability->StructureVersion.Minor);

    printf("    ProtocolVersion.Major{%d}\n",
        capability->ProtocolVersion.Major);

    printf("    ProtocolVersion.Minor{%d}\n",
        capability->ProtocolVersion.Minor);

    printf("    HashAlgorithmBitmap{%08X}\n",
        capability->HashAlgorithmBitmap);

    printf("    SupportedEventLogs{%08X}\n",
        capability->SupportedEventLogs);

    printf("    TPMPresentFlag{%d}\n",
        capability->TPMPresentFlag);

    printf("    MaxCommandSize{%d}\n",
        capability->MaxCommandSize);

    printf("    MaxResponseSize{%d}\n",
        capability->MaxResponseSize);

    printf("    ManufacturerID{%08X}\n",
        capability->ManufacturerID);

    printf("    NumberOfPcrBanks{%08X}\n",
        capability->NumberOfPcrBanks);

    printf("    ActivePcrBanks{%08X}\n",
        capability->ActivePcrBanks);

    printf("}\n");
}

EFI_TCG2_PROTOCOL* TCG2_GetProtocol(err_t* err)
{
    EFI_STATUS status;
    EFI_GUID guid = EFI_TCG2_PROTOCOL_GUID;
    EFI_TCG2_PROTOCOL *protocol = NULL;

    err_clear(err);

    status = uefi_call_wrapper(
        BS->LocateProtocol,
        3,
        &guid,
        NULL,
        (void**)&protocol);

    if (status != EFI_SUCCESS)
    {
        err_format(err, "failed: EFI_STATUS=%s", efi_strerror(status));
        return NULL;
    }

    return protocol;
}

EFI_STATUS TCG2_GetCapability(
    EFI_TCG2_PROTOCOL *protocol,
    EFI_TCG2_BOOT_SERVICE_CAPABILITY* capability)
{
    EFI_STATUS status = EFI_SUCCESS;

    if (!protocol)
        protocol = TCG2_GetProtocol(NULL);

    capability->Size = sizeof(EFI_TCG2_BOOT_SERVICE_CAPABILITY);
    status = (*protocol->GetCapability)(protocol, capability);

    if (status != EFI_SUCCESS)
        return status;

    return status;
}

EFI_STATUS TCG2_SubmitCommand(
    EFI_TCG2_PROTOCOL *protocol,
    IN UINT32 InputParameterBlockSize,
    IN UINT8 *InputParameterBlock,
    IN UINT32 OutputParameterBlockSize,
    IN UINT8 *OutputParameterBlock)
{
    if (!protocol)
        protocol = TCG2_GetProtocol(NULL);

    return protocol->SubmitCommand(
        protocol,
        InputParameterBlockSize,
        InputParameterBlock,
        OutputParameterBlockSize,
        OutputParameterBlock);
}

EFI_STATUS TCG2_HashLogExtendEvent(
    EFI_TCG2_PROTOCOL *protocol,
    IN UINT64 Flags,
    IN EFI_PHYSICAL_ADDRESS DataToHash,
    IN UINT64 DataToHashLen,
    IN EFI_TCG2_EVENT *EfiTcgEvent)
{
    if (!protocol)
        protocol = TCG2_GetProtocol(NULL);

    return protocol->HashLogExtendEvent(
        protocol,
        Flags,
        DataToHash,
        DataToHashLen,
        EfiTcgEvent);
}
