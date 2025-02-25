# cvmboot

### Introduction

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

### Basic operation

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

### Special Features

The ``cvmdisk`` utility supports two special features.

* Thin provisioning
* Resource disk usage (only for use with Azure Cloud)

**Thin provisioning** is performed by default but can be disabled with
the ``--no-thin-provisioning`` option. This feature converts the original
rootfs into a new thinly provisioned partition, which can be substantially
smaller than the original. By default, the original rootfs is removed but
can be preserved with the ``--no-strip`` option.

**Resource disk usage** is enabled with the ``--use-resourcce-disk`` option.
This feature improves performance by using the Azure resource disk as the
upper writable layer of the rootfs. The resource disk is a high-performance
local SDD, whereas the default device is a remote storage device.

### Main components

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

### Building

To build everything, simply type ``make`` from the top-level directory. This
step also installs the prerequisites. To install type ``make install``.

### Quick start

This section explains how to get started with ``cvmboot``.

#### <u>Obtaining a VHD image</u>

One way of obtaining a VHD image is to use the Azure CLI. The first step
is to "create" the image using your Azure subsription. The following command
creates an Ubuntu 22.04 CVM image, which is stored in Azure Cloud (don't forget
to first install ``az`` and to login with ``az login``).

```
$ az disk create -g <your-resource-group> -n myimage.vhd --image-reference Canonical:0001-com-ubuntu-confidential-vm-jammy:22_04-lts-cvm:latest --location eastus2
```

The next step is to grant access to the new image (``myimage.vhd``).

```
$ az disk grant-access --access-level Read --duration-in-seconds 3600 --name myimage.vhd --resource-group mikbras-eastus2-rg
{
  "accessSAS": "https://md-rdpf2p4n2h2l.z41.blob.storage.azure.net/glmkhtggmdln/abcd?sv=2018-03-28&sr=b&si=12c2f379-7c46-47a5-82b0-b9dac6d12b97&sig=RW5HD9HfMeR008FpbpVlshkWl9V8%2FyvaKQiCcG3uzOk%3D"
}
```
Finally, the following command downloads the image to the local machine (the
``url`` was displayed by the previous command as the "accessSAS" field).

```
azcopy copy <url>  myimage.vhd
```

The ``azcopy`` program does not perform a sparse copy. This means that
all zero-blocks occupy space on the local disk. Most VHDs are mostly empty
space so we stronlgy recommend using the ``cvmdisk azcopy`` command instead,
which creates a sparse VHD image. For example:

```
cvmdisk azcopy <url> myuimage2.vhd
```

The following command prints the space usage for the two downloaded images.

```
$ du -h myimage.vhd myimage2.vhd
31G     myimage.vhd
1.7G    /mnt/myimage.vhd
```

The image downloaded with ``cvmdisk azcopy`` consumes approximately 95% less
space on the disk.
