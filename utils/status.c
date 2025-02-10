// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <stdio.h>
#include "status.h"

const char* efi_strerror(EFI_STATUS status)
{
    switch (status)
    {
        case EFI_SUCCESS:
            return "EFI_SUCCESS";
        case EFI_LOAD_ERROR:
            return "EFI_LOAD_ERROR";
        case EFI_INVALID_PARAMETER:
            return "EFI_INVALID_PARAMETER";
        case EFI_UNSUPPORTED:
            return "EFI_UNSUPPORTED";
        case EFI_BAD_BUFFER_SIZE:
            return "EFI_BAD_BUFFER_SIZE";
        case EFI_BUFFER_TOO_SMALL:
            return "EFI_BUFFER_TOO_SMALL";
        case EFI_NOT_READY:
            return "EFI_NOT_READY";
        case EFI_DEVICE_ERROR:
            return "EFI_DEVICE_ERROR";
        case EFI_WRITE_PROTECTED:
            return "EFI_WRITE_PROTECTED";
        case EFI_OUT_OF_RESOURCES:
            return "EFI_OUT_OF_RESOURCES";
        case EFI_VOLUME_CORRUPTED:
            return "EFI_VOLUME_CORRUPTED";
        case EFI_VOLUME_FULL:
            return "EFI_VOLUME_FULL";
        case EFI_NO_MEDIA:
            return "EFI_NO_MEDIA";
        case EFI_MEDIA_CHANGED:
            return "EFI_MEDIA_CHANGED";
        case EFI_NOT_FOUND:
            return "EFI_NOT_FOUND";
        case EFI_ACCESS_DENIED:
            return "EFI_ACCESS_DENIED";
        case EFI_NO_RESPONSE:
            return "EFI_NO_RESPONSE";
        case EFI_NO_MAPPING:
            return "EFI_NO_MAPPING";
        case EFI_TIMEOUT:
            return "EFI_TIMEOUT";
        case EFI_NOT_STARTED:
            return "EFI_NOT_STARTED";
        case EFI_ALREADY_STARTED:
            return "EFI_ALREADY_STARTED";
        case EFI_ABORTED:
            return "EFI_ABORTED";
        case EFI_ICMP_ERROR:
            return "EFI_ICMP_ERROR";
        case EFI_TFTP_ERROR:
            return "EFI_TFTP_ERROR";
        case EFI_PROTOCOL_ERROR:
            return "EFI_PROTOCOL_ERROR";
        case EFI_INCOMPATIBLE_VERSION:
            return "EFI_INCOMPATIBLE_VERSION";
        case EFI_SECURITY_VIOLATION:
            return "EFI_SECURITY_VIOLATION";
        case EFI_CRC_ERROR:
            return "EFI_CRC_ERROR";
        case EFI_END_OF_MEDIA:
            return "EFI_END_OF_MEDIA";
        case EFI_END_OF_FILE:
            return "EFI_END_OF_FILE";
        case EFI_INVALID_LANGUAGE:
            return "EFI_INVALID_LANGUAGE";
        case EFI_COMPROMISED_DATA:
            return "EFI_COMPROMISED_DATA";
        default:
            return "UNKNOWN";
    }
}

void efi_puterr(EFI_STATUS status)
{
    printf("ERROR: %s\n", efi_strerror(status));
}
