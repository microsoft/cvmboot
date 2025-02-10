#!/bin/bash
prefix=/

arg0=$0

function errexit()
{
    echo "${arg0} $1"
    exit 1
}

function do_install()
{
    local src=$1
    local dest=$2/$1
    sudo install -D "${src}" "${dest}"
    if [ "$?" != "0" ]; then
        errexit "install command failed"
    fi
    echo "Created ${dest}"
}

sudo rm -rf "${prefix}"/usr/share/cvmboot

do_install usr/bin/sparsefs-mount "${prefix}"
do_install usr/bin/cvmdisk "${prefix}"
do_install usr/bin/cvmsign "${prefix}"
do_install usr/bin/cvmsign-init "${prefix}"
do_install usr/bin/cvmsign-verify "${prefix}"
do_install usr/bin/akvsign "${prefix}"
do_install usr/bin/cvmvhd "${prefix}"
do_install usr/bin/azcopy "${prefix}"

files="$(find usr/share/cvmboot -type f)"

for i in ${files}
do
    do_install "${i}" "${prefix}"
done
