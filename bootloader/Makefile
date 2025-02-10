TOP=$(abspath ..)
include $(TOP)/efi.mak

##==============================================================================
##
## cvmboot.efi - build the EFI application
##
##==============================================================================

TARGETBASE = cvmboot
TARGET = $(TARGETBASE).efi
DEFINES = $(EFI_DEFINES) -DBUILD_EFI

INCLUDES =
INCLUDES += $(EFI_INCLUDES)
INCLUDES += -I$(TOP)
INCLUDES += -I$(TOP)/libc/include
INCLUDES += -I$(TOP)/third-party/install/include

CFLAGS = -Wall -Os -Werror $(EFI_CFLAGS)

LIB64=lib64
LDFLAGS =
LDFLAGS += -L$(TOP)/utils -lcvmbootutils
#LDFLAGS += -L$(TOP)/third-party/install/$(LIB64) -lcrypto
LDFLAGS += $(TOP)/third-party/install/$(LIB64)/libcrypto.a
LDFLAGS += -L$(TOP)/libc -lc
LDFLAGS += $(EFI_LDFLAGS)

SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
OBJCOPYFLAGS = $(EFI_OBJCOPYFLAGS)

LIBS =
LIBS += $(TOP)/third-party/install/$(LIB64)/libcrypto.a

all: $(TARGET)

$(TARGET): timestamp $(OBJECTS) $(LIBS)
	ld -o $(TARGETBASE).so $(OBJECTS) $(LDFLAGS)
	@ ./linkcheck
	objcopy $(OBJCOPYFLAGS) $(TARGETBASE).so $(TARGET)
	rm -rf $(TARGETBASE).so
	cp -r $(TARGET) ../share/cvmboot/$(TARGET)

timestamp:
	$(MAKE) -C .. timestamp.h

%.o: %.c
	gcc -c $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ $<

logo:
	figlet cvmboot > logo.txt
	../cencode/cencode logo.txt logo > logo.c

##==============================================================================
##
## run -- run the application within QEMU
##
##==============================================================================

run:
	$(MAKE) install
	sudo shutdown now

##==============================================================================
##
## image -- build the image directory with the EFI application
##
##==============================================================================

image:
	rm -rf image
	mkdir -p image/EFI/BOOT
	ln -sf ../../../cvmboot.efi image/EFI/BOOT/BOOTX64.EFI
	ln -sf ../../../../helloworld/helloworld.efi image/EFI/BOOT/HELLO.EFI

##==============================================================================
##
## clean:
##
##==============================================================================

CLEAN =
CLEAN += $(TARGET)
CLEAN += $(TARGETBASE).so
CLEAN += $(OBJECTS)
CLEAN += image
CLEAN += OVMF.fd
CLEAN += $(OVMF_ZIP)

clean:
	rm -rf $(CLEAN)

distclean: clean

##==============================================================================
##
## killall qemu-system-x86_64
##
##==============================================================================

kill:
	killall qemu-system-x86_64
