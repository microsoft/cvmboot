// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "efifile.h"
#include <efi.h>
#include <efilib.h>
#include <eficon.h>
#include "console.h"

#define FILE_HANDLE_MAGIC 0X84455D41

struct _efi_file
{
    UINT32 magic;
    EFI_FILE* volume;
    EFI_FILE* file;
};

static BOOLEAN _ValidFile(IN efi_file_t* efiFile)
{
    if (efiFile &&
        efiFile->magic == FILE_HANDLE_MAGIC &&
        efiFile->file &&
        efiFile->volume)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static EFI_STATUS _GetFileSizeFromHandle(IN EFI_FILE* file, OUT UINTN* size)
{
    EFI_STATUS status = EFI_SUCCESS;
    EFI_GUID fileInfoId = EFI_FILE_INFO_ID;
    EFI_FILE_INFO fileInfobuffer;
    EFI_FILE_INFO* fileInfo = &fileInfobuffer;
    UINTN fileInfoSize = sizeof(fileInfobuffer);

    /* Check the parameters */
    if (!file || !size)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    /* Get size of file */
    {
        /* Try with buffer == sizeof(EFI_FILE_INFO) */
        status = uefi_call_wrapper(
            file->GetInfo,
            4,
            file,
            &fileInfoId,
            &fileInfoSize,
            fileInfo);

        /* Buffer too small so re-allocate and try again */
        if (status == EFI_BUFFER_TOO_SMALL)
        {
            fileInfo = AllocatePool(fileInfoSize);

            if (!fileInfo)
            {
                status = EFI_OUT_OF_RESOURCES;
                goto done;
            }

            status = uefi_call_wrapper(
                file->GetInfo,
                4,
                file,
                &fileInfoId,
                &fileInfoSize,
                fileInfo);
        }

        if (status != EFI_SUCCESS)
            goto done;

        *size = fileInfo->FileSize;
    }

done:

    if (fileInfo != &fileInfobuffer)
        FreePool(fileInfo);

    return status;
}

static EFI_STATUS _readn(efi_file_t* efiFile, void* data, UINTN size)
{
    EFI_STATUS status = EFI_SUCCESS;
    UINTN totalRead = 0;

    /* Check parameters */
    if (!_ValidFile(efiFile) || !data)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    /* Read the from the file */
    {
        unsigned char* p = (unsigned char*)data;
        unsigned char* end = p + size;

        while (p != end)
        {
            UINTN n = end - p;
            UINTN sizeRead = 0;

            if ((status = efi_file_read(
                efiFile,
                p,
                n,
                &sizeRead)) != EFI_SUCCESS)
            {
                goto done;
            }

            p += sizeRead;
            totalRead += sizeRead;
        }
    }

    if (totalRead != size)
    {
        status = EFI_DEVICE_ERROR;
        goto done;
    }

done:

    return status;
}

efi_file_t* efi_file_open(
    EFI_HANDLE imageHandle,
    const CHAR16* path,
    IN UINT64 fileMode,
    IN BOOLEAN append)
{
    EFI_STATUS status = EFI_SUCCESS;
    EFI_GUID loadedImageProtocol = LOADED_IMAGE_PROTOCOL;
    EFI_LOADED_IMAGE* loadedImage = NULL;
    EFI_GUID simple_file_system_protocol = SIMPLE_FILE_SYSTEM_PROTOCOL;
    EFI_FILE_IO_INTERFACE* file_io_interface = NULL;
    EFI_FILE* volume = NULL;
    EFI_FILE* file = NULL;
    efi_file_t* efiFile = NULL;

    /* Check parameters */
    if (!path)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    /* Obtain the LOADED_IMAGE_PROTOCOL */
    if ((status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        imageHandle,
        &loadedImageProtocol,
        (void**)&loadedImage)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Obtain the SIMPLE_FILE_SYSTEM_PROTOCOL */
    if ((status = uefi_call_wrapper(
        BS->HandleProtocol,
        3,
        loadedImage->DeviceHandle,
        &simple_file_system_protocol,
        (void**)&file_io_interface)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Open the volume */
    if ((status = uefi_call_wrapper(
        file_io_interface->OpenVolume,
        2,
        file_io_interface,
        &volume)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Open the file */
    if ((status = uefi_call_wrapper(
        volume->Open,
        5,
        volume,
        &file,
        (CHAR16*)path,
        fileMode,
        0)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Open file for append */
    if (append)
    {
        UINTN fileSize;

        /* Get size of file from file handle */
        if ((status = _GetFileSizeFromHandle(
            file,
            &fileSize)) != EFI_SUCCESS)
        {
            goto done;
        }

        /* Seek the end of the file (so data will be appended) */
        if ((status = uefi_call_wrapper(
            file->SetPosition,
            2,
            file,
            fileSize)) != EFI_SUCCESS)
        {
            goto done;
        }
    }

    /* Create and initialize efi_file_t */
    {
        if (!(efiFile = AllocatePool(sizeof(efi_file_t))))
        {
            status = EFI_OUT_OF_RESOURCES;
            goto done;
        }

        efiFile->magic = FILE_HANDLE_MAGIC;
        efiFile->volume = volume;
        efiFile->file = file;
    }

done:

    if (status != EFI_SUCCESS)
    {
        if (file)
            uefi_call_wrapper(file->Close, 1, file);

        if (volume)
            uefi_call_wrapper(volume->Close, 1, volume);
    }

    return efiFile;
}

EFI_STATUS efi_file_read(
    IN efi_file_t* efiFile,
    IN void* data,
    IN UINTN size,
    IN UINTN* sizeRead)
{
    EFI_STATUS status = EFI_SUCCESS;

    /* Check parameters */
    if (!_ValidFile(efiFile) || !data || !sizeRead)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    /* Read the data */
    if ((status = uefi_call_wrapper(
        efiFile->file->Read,
        3,
        efiFile->file,
        &size,
        data)) != EFI_SUCCESS)
    {
        goto done;
    }

    *sizeRead = size;

done:

    return status;
}

EFI_STATUS efi_file_close(efi_file_t* efiFile)
{
    EFI_STATUS status;

    if (!_ValidFile(efiFile))
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    if (efiFile->file)
        uefi_call_wrapper(efiFile->file->Close, 1, efiFile->file);

    if (efiFile->volume)
        uefi_call_wrapper(efiFile->volume->Close, 1, efiFile->volume);

    FreePool(efiFile);

done:

    return status;
}

EFI_STATUS efi_file_exists(IN EFI_HANDLE imageHandle, IN const CHAR16* path)
{
    EFI_STATUS status = EFI_SUCCESS;
    efi_file_t* efiFile = NULL;

    if (!(efiFile = efi_file_open(
        imageHandle, path, EFI_FILE_MODE_READ, FALSE)))
    {
        status = EFI_NOT_FOUND;
        goto done;
    }

done:

    if (efiFile)
        efi_file_close(efiFile);

    return status;
}

EFI_STATUS efi_file_load(
    IN EFI_HANDLE imageHandle,
    IN const CHAR16* path,
    OUT void** data,
    OUT UINTN* size)
{
    EFI_STATUS status = EFI_UNSUPPORTED;
    efi_file_t* efiFile = NULL;

    /* Check parameters */
    if (!path || !data || !size)
    {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }

    /* Initialize output variables */
    *data = NULL;
    *size = 0;

    /* Open the file for read */
    if (!(efiFile = efi_file_open(
        imageHandle, path, EFI_FILE_MODE_READ, FALSE)))
    {
        status = EFI_NOT_FOUND;
        goto done;
    }

    /* Get the size of the file */
    if ((status = _GetFileSizeFromHandle(
        efiFile->file,
        size)) != EFI_SUCCESS)
    {
        goto done;
    }

    /* Allocate a buffer to hold file in memory (allocate extra byte) */
    if (!(*data = AllocatePool(*size + 2)))
    {
        status = EFI_OUT_OF_RESOURCES;
        goto done;
    }

    /* Zero terminate */
    ((char*)*data)[*size] = '\0';
    ((char*)*data)[*size+1] = '\0';

    /* Read the entire file */
    if ((status = _readn(efiFile, *data, *size)) != EFI_SUCCESS)
    {
        goto done;
    }

    status = EFI_SUCCESS;

done:

    if (status != EFI_SUCCESS)
    {
        if (*data)
            FreePool(*data);

        *data = NULL;
        *size = 0;
    }

    if (efiFile)
        efi_file_close(efiFile);

    return status;
}
