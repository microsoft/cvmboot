##==============================================================================
##
## efi.mak -- build definitions needed by EFI
##
##==============================================================================

EFI_DEFINES += -DEFI_FUNCTION_WRAPPER
EFI_DEFINES += -DGNU_EFI_USE_MS_ABI

EFI_CFLAGS += -fpic
EFI_CFLAGS += -fno-builtin
EFI_CFLAGS += -std=gnu89
EFI_CFLAGS += -fno-stack-protector
EFI_CFLAGS += -fno-strict-aliasing
EFI_CFLAGS += -fshort-wchar
EFI_CFLAGS += -nostdinc
EFI_CFLAGS += -mno-red-zone
EFI_CFLAGS += -fvisibility=hidden
EFI_CFLAGS += -fno-asynchronous-unwind-tables
EFI_CFLAGS += -maccumulate-outgoing-args
EFI_CFLAGS += -ffunction-sections

EFI_DIR = $(TOP)/third-party/gnu-efi
EFI_VERSION = $(shell cat $(EFI_DIR)/VERSION)
EFI_SRCDIR = $(EFI_DIR)/gnu-efi-$(EFI_VERSION)
EFI_ARCHDIR = $(EFI_SRCDIR)/x86_64

EFI_INCLUDES += -I$(EFI_SRCDIR)/inc
EFI_INCLUDES += -I$(EFI_SRCDIR)/inc/protocol
EFI_INCLUDES += -I$(EFI_SRCDIR)/inc/x86_64

EFI_LDFLAGS += -fvisibility=hidden
EFI_LDFLAGS += -nostdlib
EFI_LDFLAGS += -znocombreloc
EFI_LDFLAGS += -shared
EFI_LDFLAGS += -Bsymbolic
EFI_LDFLAGS += -T $(EFI_DIR)/elf_x86_64_efi.lds
EFI_LDFLAGS += -L$(EFI_ARCHDIR)/gnuefi
EFI_LDFLAGS += -L$(EFI_ARCHDIR)/lib
EFI_LDFLAGS += $(EFI_ARCHDIR)/gnuefi/crt0-efi-x86_64.o
EFI_LDFLAGS += $(shell gcc -print-libgcc-file-name)
EFI_LDFLAGS += -lefi -lgnuefi

EFI_OBJCOPYFLAGS += -j .text
EFI_OBJCOPYFLAGS += -j .sdata
EFI_OBJCOPYFLAGS += -j .data
EFI_OBJCOPYFLAGS += -j .dynamic
EFI_OBJCOPYFLAGS += -j .dynsym
EFI_OBJCOPYFLAGS += -j .rel*
EFI_OBJCOPYFLAGS += -j .rela*
EFI_OBJCOPYFLAGS += -j .reloc
EFI_OBJCOPYFLAGS += -j .eh_frame
EFI_OBJCOPYFLAGS += -j .vendor_cert
EFI_OBJCOPYFLAGS += --target efi-app-x86_64
