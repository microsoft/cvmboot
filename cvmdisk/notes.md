qemu-img convert vhdx -O vhdx vhd

sudo dd if=/dev/sdb of=image bs=512 conv=sparse status=progress

# See most recent boot log
journalctl -b
