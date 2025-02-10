#!/bin/bash

if [ "$#" == "1" ]; then
    prefix=$1
else
    prefix=/
fi

arg0=$0

function errexit()
{
    echo "${arg0} $1"
    exit 1
}

function install_azcopy()
{
    local prefix=$(realpath "$1")
    local savedir="$(pwd)"
    local target=${prefix}/usr/bin/azcopy
    local tmpdir=$(mktemp -d)

    if [ ! -d "${tmpdir}" ]; then
        errexit "Failed to create temporary directory"
    fi

    cd "${tmpdir}" || errexit "failed to change diretory to ${tmpdir}"

    local pkg="azcopy.tar.gz"

    echo "Downloading ${pkg}..."

    wget --quiet https://aka.ms/downloadazcopy-v10-linux -O "${pkg}" || errexit "download failed"

    if [ ! -f "${pkg}" ]; then
        errexit "Failed to download azcopy package"
    fi

    tar zxf "${pkg}" --one-top-level=azcopy || errexit "untar failed"

    sudo mkdir -p $(dirname ${target})
    sudo cp azcopy/*/azcopy "${target}" || errexit "install failed"

    cd "${savedir}"
    sudo rm -rf "${tmpdir}"

    if [ ! -x "${target}" ]; then
        errexit "Cannot find ${target}"
    fi

    echo "Created ${target}"
}

install_azcopy "${prefix}"
