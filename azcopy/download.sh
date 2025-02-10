#!/bin/bash

arg0=$0

function errexit()
{
    echo "${arg0} $1"
    exit 1
}

function download_azcopy()
{
    local pkg="azcopy.tar.gz"

    echo "Downloading ${pkg}..."

    wget --quiet https://aka.ms/downloadazcopy-v10-linux -O "${pkg}" || errexit "download failed"

    if [ ! -f "${pkg}" ]; then
        errexit "Failed to download azcopy package"
    fi

    tar zxf "${pkg}" --one-top-level=azcopy-dir || errexit "untar failed"

    cp azcopy-dir/*/azcopy .

    echo "Created azcopy"
    rm -rf azcopy-dir
    rm -rf azcopy.tar.gz
}

download_azcopy "${prefix}"
