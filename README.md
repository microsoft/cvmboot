# cvmboot

The cvmboot project provides ephemeral disk integrity protection for Linux confidential virtual machines (CVMs). The project provides two main tools: (1) a disk utility for instrumenting a virtual hard disk (VHD) with disk integrity protection, and (2) a UEFI boot loader that provides the root of trust for attestable booting through the trusted platform module (TPM) and TCG2 log. The project offers three main benefits: (1) attestable disk integrity protection, (2) storage backend savings through thin provisioning, and (3) optimized disk write performance through utilization of the local resource disk for ephemeral writes. In short, cvmboot makes disk images attestable, smaller, and faster. Although cvmboot was created for confidential virtual machines, it offers the same benefits for conventional virtual machines. The cvmboot project is built on standard Linux tools, including dm-verity, dm-crypt, dm-thin, and dm-snapshot.

The boot loader and its associated components form a secure boot chain, which
provides integrity protection and emphemerality for the root file system
(rootfs). Changes to the rootfs are ephemeral (i.e., discarded on VM shut
down). The rootfs is implemented by two distinct partitions.

* A read-only, integrity-protected OS partition
* A writable, ephemeral, encrypted scratch partition

These two partitions are joined into a single device by ``dm-snapshot`` and
mounted on the root directory.

The ``cvmdisk`` tool is used to initialize a disk image for CVM deployment.
The following command prepares a disk image.


```
    $ sudo cvmdisk prepare <disk>
```

This command installs the boot loader and associated components on the EFI
system partition (ESP) and makes various changes to the rootfs partition. The
resulting image may be published and used as a base image for the protect step,
discussed below. The above command can also specify TPM events, users to be
created, and the hostname for the disk.

The protect step is performed by the following command.

```
    $ sudo cvmdisk protect <disk> <private-key> <public-key>
```

This command creates two partitions (the rootfs verity partition and the
dm-snapshot copy-on-write partition) and signs the ESP bundle (more on this
later).

Both steps can be performed by a single command.

```
    $ sudo cvmdisk init <disk> <private-key> <public-key>
```

The **cvmboot** boot loader is the root of trust for the boot chain. The
boot loader is concerned with the following files on the ESP (placed there
by the prepare and protect steps).

```
    /EFI/BOOT/BOOTX64.EFI       # cvmboot boot loader
    /EFI/verityboot.cpio        # CPIO archive containing boot components
    /EFI/verityboot.cpio.sig    # digital signature of verityboot.cpio
```

The ``verityboot.cpio`` is a CPIO archive that contains the following files.

```
    vmlinuz             # Linux kernel image
    cmdline             # command-line passed to the Linux kernel
    initrd.img          # initial ramdisk passed to the Linux kernel
    events              # TPM events to be extended and logged
    identity            # Contains fields for constructing the os-image-identity JSON
    rootfs              # contains the root hash of the rootfs partition
    version             # version of the Linux kernel
```

The boot loader directly boots the Linux kernel by performing the following
steps.

- Loads ``cvmboot.cpio`` into memory
- Loads ``cvmboot.cpio.sig`` into memory
- Computes the digest of ``cvmboot.cpio``
- Verifies the signature of the digest (using ``cvmboot.cpio.sig``)
- Performs PCR/log measurements/extensions (using ``events`` and ``identity`` files)
- Loads ``vmlinuz`` from the memory-resident ``cvmboot.cpio``
- Loads ``cmdline`` from the memory-resident ``cvmboot.cpio``
- Loads ``initrd.img`` from the memory-resident ``cvmboot.cpio``
- Starts the kernel with the ``cmdline`` and ``initrd.img`` parameters.

The kernel begins executing the initial ramdisk (``initrd.img``), which is
responsible for setting up the ephemeral rootfs and booting the system.

If the ``events`` file is found in the ``cvmboot.cpio``, then the boot loader
processes the TPM events from that file. Each line specifies a PCR to be
extended and associated data to be added to the TCG log. If the ``events``
file is not found, then the signer (from ``cvmboot.cpio.sig``) is extended
into PCR-11 and added to the TCG log. The events file has the following format.

```
PCR11:string:"os-image-identity":{"signer":"<os-image-signer>","svn":"1","diskId":"singularity.ubuntu-22.04","eventVersion":"1"}
PCR11:string:"node-policy-identity":{"signer":"<node-policy-signer>","svn":"1","policyId":"openai-whisper","eventVersion":"1"}
```

## Main components

**cvmboot** provides three main components.

- **cvmdisk** - utility for preparing a CVM disk
- **cvmboot** - UEFI boot loader program
- **initramfs hooks/scripts** - sets up **verity protection** and **emphemerality** for the root partition.

## Preparation

TBD

## Operation

TBD

### Building

To build everything, simply type ``make`` from the top-level directory. This
step also installs the prerequisites. To install type ``make install``.

### Preparing a VM with the cvmboot bits

TBD
