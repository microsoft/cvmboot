# cvmboot

## Introduction

The cvmboot project provides ephemeral disk integrity protection for Linux
confidential virtual machines (CVMs). The project provides two main tools.

* a disk utility for configuring a virtual hard disk (VHD) with disk
  integrity protection, and
* a UEFI boot loader that provides the root of trust for attestable booting
  through the trusted platform module (TPM) and TCG2 log.

The project offers three main benefits:

* attestable disk integrity protection,
* storage backend savings through thin provisioning, and
* optimized disk write performance through utilization of the local resource
  disk for ephemeral writes.

In short, cvmboot makes disk images attestable, smaller, and faster. Although
cvmboot was created for confidential virtual machines, it offers the same
benefits for conventional virtual machines as well. The cvmboot project is
built on standard Linux tools, including dm-verity, dm-crypt, dm-thin, and
dm-snapshot.

## Basic operation

The boot loader and its associated components form a secure boot chain, which
provides integrity protection and emphemerality for the root file system
(rootfs). Changes to the rootfs are ephemeral (i.e., discarded on VM shut
down). The rootfs consists of two distinct partitions.

* A read-only, integrity-protected OS partition (called the "lower layer")
* A writable, ephemeral, encrypted scratch partition (called the "upper layer")

These two partitions are joined into a single device by ``dm-snapshot`` and
mounted on the root directory path.

The ``cvmdisk`` tool initializes a VHD image, for use with hypervisors or
cloud-computing environments. The following command initializes an existing
base image (``base.vhd``) and creates an initialized image (``image.vhd``).

```
$ sudo cvmdisk init base.vhd image.vhd cvmsign
```

This command installs the boot loader and associated components onto the EFI
system partition (ESP) and makes various changes to the rootfs partition. The
last argument (``cvmsign``) is the program used to sign the ``cvmboot.cpio``
file, which is located on the EFI partition. Alternatively, one may use
``akssign`` for the Azure Cloud. The ``cvmdisk init`` subcommand is a
shortcut for running ``cvmdisk prepare`` followed by ``cvmdisk protect``.

The **cvmboot** boot loader is the root of trust for the boot chain. The
boot loader is concerned with the following files on the ESP (placed there
by the prepare and protect steps).

```
    /EFI/BOOT/BOOTX64.EFI       # cvmboot boot loader
    /EFI/verityboot.cpio        # CPIO archive containing boot components
    /EFI/verityboot.cpio.sig    # digital signature of verityboot.cpio
```

The ``verityboot.cpio`` file is a CPIO archive containing the following files.

```
    vmlinuz             # Linux kernel image
    initrd              # The initial ram disk
    events              # TPM events to be extended and logged
    cvmboot.conf        # Configuration file
```

The boot loader directly boots the Linux kernel by performing the following
steps.

* Loads ``cvmboot.cpio`` into memory
* Loads ``cvmboot.cpio.sig`` into memory
* Computes the digest of ``cvmboot.cpio``
* Verifies the signature of the digest (using ``cvmboot.cpio.sig``)
* Performs PCR/log measurements/extensions as specified in the ``events`` file
* Loads ``vmlinuz`` from the memory-resident ``cvmboot.cpio``
* Loads ``cmdline`` from the memory-resident ``cvmboot.cpio``
* Loads ``initrd.img`` from the memory-resident ``cvmboot.cpio``
* Starts the kernel with the ``cmdline`` and ``initrd.img`` parameters.

The kernel begins executing the initial ram disk (``initrd.img``), which is
responsible for setting up the ephemeral rootfs and booting the system.

If the ``events`` file is found in the ``cvmboot.cpio``, then the boot loader
processes the TPM events from that file. Each line specifies a PCR to be
extended and associated data to be added to the TCG log. If the ``events``
file is not found, then the signer (from ``cvmboot.cpio.sig``) is extended
into PCR-11 and added to the TCG log. The events file has the following format.

```
PCR11:string:"os-image-identity":{"signer":"<filled-in-by-boot-loader>","svn":"1","diskId":"singularity.ubuntu-22.04","eventVersion":"1"}
PCR11:string:"node-policy-identity":{"signer":"<node-policy-signer>","svn":"1","policyId":"openai-whisper","eventVersion":"1"}
```

## Special Features

The ``cvmdisk`` utility supports two special features.

* Thin provisioning
* Resource disk usage (only for use with Azure Cloud)

**Thin provisioning** is performed by default but can be disabled with
the ``--no-thin-provisioning`` option. This feature converts the original
rootfs into a new thinly provisioned partition, which can be substantially
smaller than the original. By default, the original rootfs is removed but
can be preserved with the ``--no-strip`` option.

**Resource disk usage** is enabled with the ``--use-resource-disk`` option.
This feature improves performance by using the Azure resource disk as the
upper writable layer of the rootfs. The resource disk is a high-performance
local SDD, whereas the default device is a remote storage device.

## Main components

**cvmboot** provides three main components.

* **cvmdisk** - utility for preparing a CVM disk
* **cvmboot** - UEFI boot loader program
* **initramfs hooks/scripts** - sets up rootfs

Depending on the cvmdisk options, the rootfs may utilize the following
dev-mapper targets.

* **dm-verity** -- provides read-only integrity protected lower layer
* **dm-thin** -- provides thin provisioning for the lower layer
* **dm-crypt** -- encrypts the writable upper layer
* **dm-snapshot** -- joins the upper and lower layers to form the rootfs

## Building

To build everything, simply type ``make`` from the top-level directory. This
step also installs the prerequisites. To install type ``make install``.

## Quick start

This section explains how to get started with ``cvmboot``.

#### <ins>Obtaining a VHD image</ins>

One way of obtaining a VHD image is to use the Azure CLI. The first step
is to "create" the image using your Azure subscription. The following command
creates an Ubuntu 22.04 CVM image, which is stored in Azure Cloud (don't forget
to first install ``az`` and to login with ``az login``).

```
$ az disk create -g <your-resource-group> -n base.vhd --image-reference Canonical:0001-com-ubuntu-confidential-vm-jammy:22_04-lts-cvm:latest --location eastus2
```

The next step is to grant access to the new image (``base.vhd``).

```
$ az disk grant-access --access-level Read --duration-in-seconds 3600 --name base.vhd --resource-group mikbras-eastus2-rg
{
  "accessSAS": "https://md-rdpf2p4n2h2l.z41.blob.storage.azure.net/glmkhtggmdln/abcd?sv=2018-03-28&sr=b&si=12c2f379-7c46-47a5-82b0-b9dac6d12b97&sig=RW5HD9HfMeR008FpbpVlshkWl9V8%2FyvaKQiCcG3uzOk%3D"
}
```
Finally, the following command downloads the image to the local machine (the
``url`` was displayed by the previous command as the "accessSAS" field).

```
azcopy copy <url> base.vhd
```

The ``azcopy`` program does not perform a sparse copy. This means that
all zero-blocks occupy space on the local disk. Most VHDs are mostly empty
space so we strongly recommend using the ``cvmdisk azcopy`` command instead,
which creates a sparse VHD image. For example:

```
cvmdisk azcopy <url> base2.vhd
```

The following command prints the space usage for the two downloaded images.

```
$ du -h base.vhd base2.vhd
31G     base.vhd
1.7G    /mnt/base.vhd
```

The image downloaded with ``cvmdisk azcopy`` consumes approximately 95% less
space on the disk.

#### <ins>Customizing the base image</ins>

Any customizations to the base image should be performed at this stage. To
display the partitions in the disk, use ``fdisk``.

```
$ fdisk base.vhd
GPT PMBR size mismatch (8388607 != 62916608) will be corrected by write.
The backup GPT table is not on the end of the device.
Disk base.vhd: 30 GiB, 32213303808 bytes, 62916609 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
Disklabel type: gpt
Disk identifier: CB28D32C-BC2A-4FD9-AD3D-FC3E7004314E

Device       Start     End Sectors Size Type
base.vhd1  2107392 8388574 6281183   3G Linux filesystem
base.vhd14    2048   10239    8192   4M BIOS boot
base.vhd15   10240 2107391 2097152   1G EFI System

Partition table entries are not in disk order.
```

Notice that the Linux rootfs partition is only 3G while 26G of the disk
is unused. So the first customization will be to expand the rootfs to
consume the unused space.

```
$ sudo cvmdisk expand-root-partition base.vhd
```

The rootfs partition is now be 26G.

One may now use the ``cvmdisk shell`` command to perform any special
customizations.

```
$ sudo cvmdisk shell base.vhd
# ls
bin   dev  home  lib32  libx32      media  opt   root  sbin  srv  tmp  var
boot  etc  lib   lib64  lost+found  mnt    proc  run   snap  sys  usr
# _
```

Once shelled into the base image, additional software may be installed and
any necessary configuration can be made.

#### <ins>Preparing and protecting the image</ins>

Next we prepare the new base VHD image (``image.vhd``).

```
$ sudo cvmdisk prepare base.vhd image.vhd
```

The next step is to protect the image.

```
$ sudo cvmdisk protect image.vhd cvmsign
roothash: 66edc2dedf7a1aecc608d49cb2185415be3c2ba8b075675f9cc8222b41a846f3
Created /EFI/cvmboot.cpio.sig
signer=b94fcf0d38086150b7ffcd7f18971b0909821d3835b27cf46f18084e6719b87e
LOG[11:a9f39198dbb7e8da7491251583070a51f61b7e4c1870040405ec5758e7e90825]
PCR[11]=9e024009db225adac339988d8a3809c6b5589530ca101516e64c3eb99e8acb31
```

This signs the boot loader payload file (``cvmboot.cpio``) and
creates ``cvmboot.cpio.sig``. It also prints out measurements that will
be needed later for performing attestation.

The ``cvmsign`` program is used to sign ``cvmsign.cpio``. For Azure Cloud one
may use ``akvsign`` instead, which uses Azure Key Vault (AKV) to perform the
signing without disclosing the private key to the local machine. Using
``akvsign`` requires setting up login credentials with the AKV service.
