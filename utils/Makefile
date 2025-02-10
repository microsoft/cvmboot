TOP=$(abspath ..)
include $(TOP)/efi.mak

.PHONY: roothash

TARGET = libcvmbootutils.a
CFLAGS = -Wall -Werror -Os $(EFI_CFLAGS) -U_FORTIFY_SOURCE
DEFINES =
INCLUDES = $(EFI_INCLUDES)
INCLUDES += -I$(TOP)/libc/include
INCLUDES += -I$(TOP)/third-party/install/include
SOURCES += $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

$(TARGET): $(OBJECTS)
	ar rv $(TARGET) $(OBJECTS)

%.o: %.c
	gcc -c $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ $<

clean:
	rm -f $(TARGET) $(OBJECTS)

distclean: clean
