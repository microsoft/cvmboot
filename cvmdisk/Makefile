.PHONY: verity
TOP=$(abspath ..)
DISK=/dev/sdb
#DISK=vhd
ifdef TRACE
    TRACE_OPT=--trace
endif

.PHONY: roothash

TARGET = cvmdisk

SOURCES += $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS += -g -O3 -Wall -Werror

DEFINES =

INCLUDES = -I$(TOP) -I$(TOP)/third-party/install/include

LDFLAGS =
LDFLAGS += -L$(TOP)/utils -lcvmbootutils
LDFLAGS += -L$(TOP)/common -lcvmbootcommon
LDFLAGS += $(TOP)/third-party/install/lib64/libcrypto.a
LDFLAGS += $(TOP)/third-party/install/lib/libcrc.a

$(TARGET): timestamp version $(OBJECTS)
	gcc $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

timestamp:
	$(MAKE) -C .. timestamp.h

version:
	$(MAKE) -C .. version.h

%.o: %.c
	gcc -c $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ $<

clean:
	rm -f $(TARGET) $(OBJECTS)

install:
	$(MAKE) -C .. install

uninstall:
	$(MAKE) -C .. uninstall

distclean: clean

##==============================================================================
##
## test rules:
##
##==============================================================================

VERITYTOOL=$(CURDIR)/cvmdisk

purge:
	sudo $(VERITYTOOL) purge $(DISK)

init:
	sudo $(VERITYTOOL) $(TRACE_OPT) init $(DISK) private.pem public.pem

verify:
	sudo $(VERITYTOOL) verify $(DISK)

superhash:
	sudo $(VERITYTOOL) superhash $(DISK)

list:
	sudo $(VERITYTOOL) list $(DISK)

verity:
	sudo $(VERITYTOOL) verity $(DISK)

list-verbose:
	sudo $(VERITYTOOL) list --verbose $(DISK)

shell:
	sudo $(VERITYTOOL) shell $(DISK)

gpt:
	sudo $(VERITYTOOL) gpt $(DISK)

reload:
	sudo $(VERITYTOOL) reload $(DISK)

fdisk:
	sudo fdisk -l $(DISK)

losetup:
	sudo $(VERITYTOOL) losetup $(DISK)

losetup_d:
	-sudo losetup -d /dev/loop6
	-sudo losetup -d /dev/loop7
	-sudo losetup -d /dev/loop8
	-sudo losetup -d /dev/loop9

test:
	sudo cvmdisk minext4 /dev/sdb

strip:
	sudo cvmdisk strip /dev/sdb new.vhd

t:
	sudo cvmdisk create-thin 

ldd:
	ldd $(TARGET)
