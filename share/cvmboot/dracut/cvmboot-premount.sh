#!/bin/sh
# cvmboot pre-mount hook for dracut (adapted from initramfs-tools)
type getarg >/dev/null 2>&1 || . /lib/dracut-lib.sh

# Compatibility: ensure panic function is available
if ! type panic >/dev/null 2>&1; then
    panic() {
        echo "$@"
        die "$@"
    }
fi

# Debug output - force to console and kmsg
exec 1>/dev/console 2>&1
echo "===== CVMBoot pre-mount: Starting... ====="
# Get ROOT from kernel cmdline if not already set
if [ -z "${ROOT}" ]; then
    for arg in $(cat /proc/cmdline); do
        case "${arg}" in
            root=*) ROOT="${arg#root=}" ;;
        esac
    done
fi
echo "ROOT=${ROOT}"
echo "cmdline: $(cat /proc/cmdline 2>&1 || echo 'no /proc/cmdline')"
echo "Available block devices:"
ls -la /dev/sd* /dev/vd* /dev/nvme* 2>&1 || echo "No block devices found"
echo "Contents of /dev/disk:"
ls -la /dev/disk/ 2>&1 || echo "No /dev/disk directory"

# Safety check: If ROOT is not set, skip cvmboot processing
if [ -z "${ROOT}" ]; then
    echo "CVMBoot: ROOT variable not set after checking cmdline, skipping cvmboot processing"
    exit 0
fi

# dev-mapper name
# CAUTION: this must match the definition of ${rootfs_ro} in root_ro.script:
#rootfs_ro=rootfs_ro
rootfs_ro=rootfs_verity

##==============================================================================
##
## find_hash_device_from_data_device(data_device)
##
##==============================================================================
find_hash_device_from_data_device()
{
    local dev=$1

    # Check format of device name: "/dev/sdb[0-9]*"
    echo ${dev} | /usr/bin/grep -q "^/dev/sd[a-z][0-9]*"
    if [ "$?" != "0" ]; then
        echo "$0: invalid device name: ${dev}"
        return 1
    fi

    # If device does not exist.
    if [ ! -b "${dev}" ]; then
        echo "$0: not a block device: ${dev}"
        return 1
    fi

    # Get the disk name from the device name (e.g., /dev/sdb1 => /dev/sdb).
    local disk=$(echo ${dev} | sed 's/[0-9]$//g')
    if [ ! -b "${disk}" ]; then
        echo "$0: not a block device: ${disk}"
        return 1
    fi

    # Get the UUID of this data device.
    local tuuid=$(fdisk -l -o DEVICE,UUID "${disk}" 2> /dev/null | grep "${dev}" | awk '{print $2}')
    if [ "$?" != "0" ]; then
        echo "$0: unexpected fdisk failure: ${dev}"
        return 1
    fi
    if [ -z "${tuuid}" ]; then
        echo "$0: unexpected fdisk failure: ${dev}"
        return 1
    fi

    local uuid=$(echo ${tuuid} | tr '[A-Z]' '[a-z]')
    if [ "$?" != "0" ]; then
        echo "$0: tr failed"
        return 1
    fi

    # Get a list of partitions for this disk.
    local partitions=$(ls ${disk}[0-9]*)
    if [ "$?" != "0" ]; then
        echo "$0: unexpected failure: cannot glob disk device names: ${disk}"
        return 1
    fi

    # Iterate partitions looking for the hash-device for this data-device.
    for hdev in ${partitions}
    do
        # if this is a hash device.
        veritysetup dump ${hdev} > /dev/null 2> /dev/null
        if [ "$?" == "0" ]; then
            # Get the UUID of this hash device.
            local huuid=$(veritysetup dump ${hdev} | grep "UUID:" | awk '{print $2}')
            if [ "$?" != "0" ]; then
                echo "$0: failed to get the UUID"
                return 1
            fi
            if [ -z "${uuid}" ]; then
                echo "$0: failed to get the UUID"
                return 1
            fi

            if [ "${huuid}" == "${uuid}" ]; then
                echo "${hdev}"
                return 0
            fi
        fi
    done

    # not found!
    return 1
}

##==============================================================================
##
## get_roothash_from_hash_device(hash_device)
##
##==============================================================================
get_roothash_from_hash_device()
{
    local dev=$1
    local hbs=4096

    # Create a temporary file to hold the hash text.
    local text=$(mktemp)
    if [ -z "${text}" ]; then
        echo "$0: /bin/mktemp failed"
        return 1
    fi

    # Get the salt from the hash device.
    dd bs=1 skip=88 count=32 if=${dev} of=${text} 2> /dev/null
    if [ "$?" != "0" ]; then
        echo "$0: dd failed: if=${dev} of=${text}"
        return 1
    fi

    # Get the second block of the hash device.
    dd bs=1 skip=${hbs} count=${hbs} seek=32 if=${dev} of=${text} 2> /dev/null
    if [ "$?" != "0" ]; then
        echo "$0: failed to read ${dev} or write ${text}"
        return 1
    fi

    # Find the root hash: SHA-256(salt + block2)
    local roothash=$(sha256sum ${text} | awk '{print $1}')
    if [ "$?" != "0" ]; then
        echo "$0: sha256sum failed on ${text}"
        return 1
    fi

    # Echo the root hash
    echo ${roothash}

    # success!
    return 0
}

##==============================================================================
##
## get_roothash_from_efi_variable(data_device)
##
##==============================================================================
get_roothash_from_efi_variable()
{
    local tmpfile=$(mktemp)
    local uuid=08b5e462-25eb-42c0-a7d1-e9f78c3c7e09

    cat /sys/firmware/efi/efivars/roothash-${uuid} > ${tmpfile}
    if [ "$?" != "0" ]; then
        echo "$0: cannot find roothash EFI variable"
        return 1
    fi

    local tmpvar=$(tail -c +4 ${tmpfile})
    if [ -z "${tmpvar}" ]; then
        echo "$0: cannot parse roothash EFI variable"
        return 1
    fi

    local roothash=$(echo ${tmpvar} | tr '[A-Z]' '[a-z]')
    if [ -z "${roothash}" ]; then
        echo "$0: EFI variable is empty"
        return 1
    fi

    echo ${roothash}

    return 0
}

##==============================================================================
##
## verity_panic(message)
##
##==============================================================================
verity_panic()
{
    local message=$1
    echo "verity_panic: ${message}"
    read
    panic "$0: panic"
}

##==============================================================================
##
## check_noexist(symlink, num)
##
##==============================================================================
check_noexist()
{
    local symlink=$1
    local num=$2
    if [ -L "${symlink}" ]; then
        verity_panic "$0: symlink exists: ${symlink} ${num}"
    fi
}

##==============================================================================
##
## activate_thin_volume(disk)
##
##==============================================================================
activate_thin_volume()
{
    local disk=$1

    # get the thin data device from the disk
    local data_dev=$(fdisk -l -o DEVICE,Type-UUID ${disk} | grep -i 136CE4AF-AFED-4F96-84FF-0651088074EE | head -1 | cut -d' ' -f1)
    if [ "$?" != "0" ]; then
        verity_panic "failed to get thin data device"
    fi

    # get the thin meta device from the disk
    local meta_dev=$(fdisk -l -o DEVICE,Type-UUID ${disk} | grep -i ED71D74E-250A-4F9F-A29B-32246F9BB43A | head -1 | cut -d' ' -f1)
    if [ "$?" != "0" ]; then
        verity_panic "failed to get thin data device"
    fi

    local num_data_sectors=$(blockdev --getsz "${data_dev}")
    if [ "$?" != "0" ]; then
        verity_panic "failed to get number of data sectors"
    fi

    # Thin block size in units of 512-bytes
    local block_size=1024
    local low_water_mark=1024
    local num_thin_sectors=$(cat /etc/cvmboot-thin-sectors)

    #echo "block_size=${block_size}"
    #echo "low_water_mark=${low_water_mark}"
    #echo "data_dev=${data_dev}"
    #echo "meta_dev=${meta_dev}"
    #echo "num_data_sectors=${num_data_sectors}"
    #echo "num_thin_sectors=${num_thin_sectors}"
    #read

    # activate /dev/mapper/rootfs_thin_pool
    dmsetup create rootfs_thin_pool --table "0 ${num_data_sectors} thin-pool ${meta_dev} ${data_dev} ${block_size} ${low_water_mark} 1 read_only"
    if [ "$?" != "0" ]; then
        verity_panic "failed to activate thin-pool: ${meta_dev} ${data_dev}"
    fi

    # activate /dev/mapper/rootfs_thin volume
    dmsetup create rootfs_thin --table "0 ${num_thin_sectors} thin /dev/mapper/rootfs_thin_pool 0"
}

##==============================================================================
##
## find_hash_device(disk)
##
##==============================================================================
find_hash_device()
{
    local disk=$1

    fdisk -l -o DEVICE,Type-UUID ${disk} | grep -i 3416E185-0EFA-4BA5-BF43-BE206E7F9AF0 | head -1 | cut -d' ' -f1
}

##==============================================================================
##
## test_thin_data_device(disk)
##
##==============================================================================
test_thin_data_device()
{
    local disk=$1

    fdisk -l -o DEVICE,Type-UUID ${disk} | grep -q 136CE4AF-AFED-4F96-84FF-0651088074EE
}

##==============================================================================
##
## create_snapshot_device()
##
##==============================================================================
create_snapshot_device()
{
    # Create dm-snapshot device combining verity (read-only) + crypt (writable)
    origin="/dev/mapper/${rootfs_ro}"
    cowdev="/dev/mapper/rootfs_crypt"
    sectors=$(blockdev --getsz "${origin}")
    
    echo "CVMBoot: Creating dm-snapshot with ${sectors} sectors"
    echo "CVMBoot:   origin=${origin}"
    echo "CVMBoot:   cowdev=${cowdev}"
    
    if ! dmsetup create rootfs_snapshot --table "0 ${sectors} snapshot ${origin} ${cowdev} N 8"; then
        echo "CVMBoot: ERROR: Failed to create dm-snapshot device"
        echo "CVMBoot: Dumping debug information:"
        dmsetup info ${rootfs_ro}
        ls -la /dev/mapper/
        blkid
        verity_panic "Failed to create dm-snapshot device"
    fi
    
    echo "CVMBoot: Created dm-snapshot device: /dev/mapper/rootfs_snapshot"
}

##==============================================================================
##
## main:
##
##==============================================================================

##
## Extract the UUID from the ROOT variable
##
uuid=${ROOT#UUID=}
if [ -z "${uuid}" ]; then
    verity_panic "$0: unexpected ROOT variable format: ${ROOT}"
fi

##
## Find the device with this UUID using blkid
##
rootdev=$(blkid -U "${uuid}")
if [ -z "${rootdev}" ]; then
    verity_panic "$0: cannot find device with UUID: ${uuid}"
fi

##
## Find the disk (e.g., /dev/sda from /dev/sda1)
##
disk=$(echo ${rootdev} | sed 's/[0-9]*$//')

##
## Choose the data-device (thin-volume or raw partition)
##
test_thin_data_device ${disk}
if [ "$?" == "0" ]; then
    activate_thin_volume ${disk}
    datadev=/dev/mapper/rootfs_thin
else
    # Try generic Linux filesystem type first
    datadev=$(fdisk -l -o DEVICE,Type-UUID ${disk} | grep -i 0FC63DAF-8483-4772-8E79-3D69D8477DE4 | head -1 | cut -d' ' -f1)
    # If not found, try Linux x86-64 root partition type (used by Mariner)
    if [ -z "${datadev}" ]; then
        datadev=$(fdisk -l -o DEVICE,Type-UUID ${disk} | grep -i 4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709 | head -1 | cut -d' ' -f1)
    fi
    if [ -z "${datadev}" ]; then
        verity_panic "failed to get the rootfs partition"
    fi
fi

##
## Find the hash-device
##
hashdev=$(find_hash_device ${disk})
if [ "$?" != "0" ]; then
    verity_panic "failed to find hash device"
fi

##
## Recompute the roothash from the hash device
##
roothash=$(get_roothash_from_hash_device ${hashdev})
if [ "$?" != "0" ]; then
    verity_panic "$0: failed to recover the roothash from ${hashdev}"
fi

##
## Mount the "efivar" file system (if not already mounted):
##
mount -t efivarfs none /sys/firmware/efi/efivars
if [ "$?" != "0" ]; then
    echo "error: failed to mount efivars"
    # ATTN: it might be mounted already!
fi

##
## Get the roothash from the EFI variable:
##
efivar_roothash=$(get_roothash_from_efi_variable)
if [ "$?" != "0" ]; then
    verity_panic "$0: failed to get roothash from EFI variable"
fi

##
## Fail if roothash does not match roothash from EFI variable:
##
if [ "${roothash}" != "${efivar_roothash}" ]; then
    verity_panic "bad roothash: ${roothash}/${efivar_roothash}"
fi

##==============================================================================
##
## Open the verity device onto /dev/mapper/${rootfs_ro}. Note: this command
## recreates ${uuid_symlink} as an unexpected side effect.
##
## Resulting topology:
##
##                 [ /dev/mapper/${rootfs_ro} ] <- EXT4 blocks
##                 [ dm-verity driver         ]
##                   /                       \
##       [ /dev/sda? (ext4) ]         [ /dev/sda? (verity) ]
##
##==============================================================================

echo "CVMBoot: Opening dm-verity device: datadev=${datadev} name=${rootfs_ro} hashdev=${hashdev}"
veritysetup open "${datadev}" "${rootfs_ro}" "${hashdev}" "${efivar_roothash}"
if [ "$?" != "0" ]; then
    verity_panic "$0: veritysetup open: ${datadev} ${hashdev} ${efivar_roothash}"
fi
echo "CVMBoot: dm-verity device opened successfully: /dev/mapper/${rootfs_ro}"

##==============================================================================
##
## Create writable overlay using dm-snapshot
##
## This creates a writable snapshot device on top of the read-only dm-verity
## device, allowing writes to be stored in a dm-crypt encrypted partition.
##
##==============================================================================

rootfs_rw="rootfs_snapshot"
# Check if resource disk mode is enabled
if [ ! -f "/etc/cvmboot-resource-disk" ]; then
    echo "CVMBoot: Creating writable overlay with dm-snapshot"

    # Get the physical disk device for finding partitions
    # When using thin provisioning, datadev is /dev/mapper/rootfs_thin, but we need
    # the physical disk (/dev/sda) to find the ROOTFS-UPPER partition
    if echo "${datadev}" | grep -q "^/dev/mapper/"; then
        # datadev is a mapper device, use the already-determined physical disk
        phys_disk="${disk}"
    else
        # datadev is a physical partition, derive disk from it
        phys_disk=$(echo "${datadev}" | sed 's/[0-9]*$//')
    fi
    echo "CVMBoot: Physical disk device: ${phys_disk}"

    # Find the upper layer partition (PARTLABEL "ROOTFS-UPPER")
    echo "CVMBoot: Searching for upper layer partition with PARTLABEL 'ROOTFS-UPPER'"
    echo "CVMBoot: Scanning partitions: ${phys_disk}*"

    upper_partition=""
    for part in ${phys_disk}*; do
        echo "CVMBoot: Checking partition: ${part}"

        if [ ! -b "${part}" ]; then
            echo "CVMBoot:   -> Not a block device, skipping"
            continue
        fi

        echo "CVMBoot:   -> Is block device, checking partition label..."

        # Get partition label
        part_label=$(blkid -s PARTLABEL -o value "${part}" 2>/dev/null)
        echo "CVMBoot:   -> PARTLABEL: ${part_label}"

        # Also show all attributes for debugging
        blkid "${part}" 2>/dev/null | head -1

        if [ "${part_label}" = "ROOTFS-UPPER" ]; then
            echo "CVMBoot:   -> MATCH! Found upper layer partition"
            upper_partition="${part}"
            break
        else
            echo "CVMBoot:   -> No match, continuing search..."
        fi
    done

    if [ -z "${upper_partition}" ]; then
        echo "CVMBoot: ERROR: Upper layer partition not found"
        echo "CVMBoot: Listing all partitions with fdisk:"
        fdisk -l "${phys_disk}" 2>&1
        echo "CVMBoot: Listing all block devices with blkid:"
        blkid 2>&1
        verity_panic "Failed to find upper layer partition with PARTLABEL 'ROOTFS-UPPER'"
    fi

    echo "CVMBoot: Found upper layer partition: ${upper_partition}"

    # Generate ephemeral key for dm-crypt
    keyfile="/tmp/cvmboot-keyfile"
    dd if=/dev/urandom of="${keyfile}" bs=512 count=1 >/dev/null 2>&1
    if [ ! -f "${keyfile}" ]; then
        verity_panic "Failed to create ephemeral key: ${keyfile}"
    fi

    echo "CVMBoot: Generated ephemeral encryption key"

    # Open dm-crypt device for writable layer
    if ! cryptsetup open --type plain "${upper_partition}" rootfs_crypt \
         --batch-mode --cipher=aes-xts-plain64 --key-size=512 --key-file="${keyfile}"; then
        verity_panic "Failed to open dm-crypt device for upper layer"
    fi

    echo "CVMBoot: Opened dm-crypt device: /dev/mapper/rootfs_crypt"

    # Create dm-snapshot device
    create_snapshot_device
else
    echo "CVMBoot: Resource disk mode enabled, creating writable overlay on /dev/sdb"
    
    # Resource disk setup - create encrypted overlay on /dev/sdb
    resource_disk="/dev/sdb"
    resource_partition="${resource_disk}1"
    
    # Fail if the resource disk does not exist
    if [ ! -b "${resource_disk}" ]; then
        verity_panic "Resource disk not found: ${resource_disk}"
    fi
    
    echo "CVMBoot: Found resource disk: ${resource_disk}"
    
    # Remove existing partitions if they exist
    if [ -b "${resource_partition}" ]; then
        echo "CVMBoot: Removing existing partition on resource disk"
        if ! parted -s "${resource_disk}" rm 1; then
            verity_panic "Failed to remove first partition from resource disk"
        fi
    fi
    
    # Remove second partition if any (check partition 1 again as partitions renumber)
    if [ -b "${resource_partition}" ]; then
        echo "CVMBoot: Removing second partition on resource disk"
        if ! parted -s "${resource_disk}" rm 1; then
            verity_panic "Failed to remove second partition from resource disk"
        fi
    fi
    
    # Create first NTFS partition (10%)
    echo "CVMBoot: Creating first NTFS partition (10%)"
    if ! parted -s "${resource_disk}" mkpart primary ntfs "0%" "10%"; then
        verity_panic "Failed to create first NTFS partition"
    fi
    
    # Create second NTFS partition (remaining space)
    echo "CVMBoot: Creating second NTFS partition (90%)"
    if ! parted -s "${resource_disk}" mkpart primary ntfs "10%" "100%"; then
        verity_panic "Failed to create second NTFS partition"
    fi
    
    sleep 1
    
    # Get partition geometry for loop device
    sectsize=$(fdisk -l "${resource_disk}" | grep "Units:" | awk '{print $8}')
    if [ -z "${sectsize}" ]; then
        verity_panic "Cannot determine sector size"
    fi
    
    start=$(fdisk -l "${resource_disk}" | tail -n1 | awk '{print $2}')
    if [ -z "${start}" ]; then
        verity_panic "Cannot determine starting sector for second partiion"
    fi
    
    sectors=$(fdisk -l "${resource_disk}" | tail -n1 | awk '{print $4}')
    if [ -z "${sectors}" ]; then
        verity_panic "Cannot determine number of sectors for second partition"
    fi
    
    echo "CVMBoot: Partition geometry - sector_size=${sectsize} start=${start} sectors=${sectors}"
    
    # Delete second NTFS partition (we'll use it via loop device)
    echo "CVMBoot: Removing second NTFS partition for loop device setup"
    if ! parted -s "${resource_disk}" rm 2; then
        verity_panic "Failed to delete second NTFS partition"
    fi
    sleep 1
    
    # Format first NTFS partition
    sleep 1
    echo "CVMBoot: Formatting first NTFS partition"
    if ! mkfs.ntfs -f "${resource_partition}"; then
        verity_panic "Failed to format NTFS partition: ${resource_partition}"
    fi
    
    # Create loop device for the encrypted storage area
    offset=$((${start} * ${sectsize}))
    size=$((${sectors} * ${sectsize}))
    
    echo "CVMBoot: Creating loop device - offset=${offset} size=${size}"
    loop=$(losetup -o "${offset}" --sizelimit "${size}" -b "${sectsize}" -f "${resource_disk}" --show)
    if [ -z "${loop}" ]; then
        verity_panic "Failed to create loop device"
    fi
    
    echo "CVMBoot: Created loop device: ${loop}"
    
    # Generate ephemeral key for dm-crypt
    keyfile="/tmp/cvmboot-resource-keyfile"
    dd if=/dev/urandom of="${keyfile}" bs=512 count=1 >/dev/null 2>&1
    if [ ! -f "${keyfile}" ]; then
        verity_panic "Failed to create ephemeral key: ${keyfile}"
    fi
    
    echo "CVMBoot: Generated ephemeral encryption key"
    
    # Open dm-crypt device for writable layer
    if ! cryptsetup open --type plain "${loop}" rootfs_crypt \
         --batch-mode --cipher=aes-xts-plain64 --key-size=512 --key-file="${keyfile}" \
         --perf-no_write_workqueue; then
        verity_panic "Failed to open dm-crypt device on resource disk"
    fi
    
    echo "CVMBoot: Opened dm-crypt device: /dev/mapper/rootfs_crypt"
    
    # Create dm-snapshot device
    create_snapshot_device
fi

# Override sysroot.mount to explicitly use rootfs_rw device
echo "CVMBoot: Configuring sysroot.mount to use /dev/mapper/${rootfs_rw}"
mkdir -p /run/systemd/system/sysroot.mount.d/
if [ "$?" != "0" ]; then
    verity_panic "failed to create sysroot.mount override directory"
fi

cat > /run/systemd/system/sysroot.mount.d/rootfsrw.conf <<EOF
[Mount]
What=/dev/mapper/${rootfs_rw}
Options=rw,relatime
EOF
if [ "$?" != "0" ]; then
    verity_panic "failed to write sysroot.mount override config"
fi
echo "CVMBoot: sysroot.mount configured successfully"

# Reload systemd to pick up the configuration changes
echo "CVMBoot: Reloading systemd configuration"
systemctl daemon-reload
if [ "$?" != "0" ]; then
    verity_panic "systemctl daemon-reload failed"
fi
echo "CVMBoot: systemd configuration reloaded"

# Trigger udev for block devices to ensure systemd device units are created
echo "CVMBoot: Triggering udev for block devices"
udevadm trigger --action=add --subsystem-match=block
if [ "$?" != "0" ]; then
    echo "CVMBoot: WARNING: udevadm trigger failed (non-fatal)"
fi

udevadm settle --timeout=30
if [ "$?" != "0" ]; then
    echo "CVMBoot: WARNING: udevadm settle timed out (non-fatal)"
fi
echo "CVMBoot: udev processing complete"

# Verify the setup
echo "CVMBoot: Verifying rootfs_rw device setup"
if [ ! -b "/dev/mapper/${rootfs_rw}" ]; then
    verity_panic "/dev/mapper/${rootfs_rw} does not exist"
fi

if [ -n "${rootfs_rw}" ]; then
    dmsetup info ${rootfs_rw}
    if [ "$?" != "0" ]; then
        verity_panic "dmsetup info failed for ${rootfs_rw}"
    fi
fi

echo "CVMBoot: Setup complete - ${rootfs_rw} device will be mounted as root"
