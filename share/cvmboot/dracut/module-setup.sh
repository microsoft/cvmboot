#!/bin/bash
# cvmboot dracut module setup script

check() {
    return 0
}

depends() {
    # Need systemd-udevd for /dev/disk/by-uuid symlinks
    echo systemd
    return 0
}

install() {
    inst_hook cmdline 20 "$moddir/cvmboot-cmdline.sh"
    inst_hook pre-mount 20 "$moddir/cvmboot-premount.sh"
    
    # Install required binaries
    inst_multiple veritysetup cryptsetup blockdev grep awk sed tr fdisk udevadm readlink cut head mktemp sort tail dd sha256sum blkid dmsetup
    
    # Install libraries needed by these binaries
    inst_libdir_file "libcryptsetup.so*" "libblkid.so*" "libdevmapper.so*"
}
