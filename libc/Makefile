TOP=$(abspath ..)
include $(TOP)/defs.mak
include $(TOP)/efi.mak

ARCHIVE = libc.a
CFLAGS = -Wall -Werror -Os $(EFI_CFLAGS) -U_FORTIFY_SOURCE
DEFINES = $(EFI_DEFINES) -DBUILD_EFI
INCLUDES = -I./include $(EFI_INCLUDES)
SOURCES += $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

all: $(ARCHIVE)

$(ARCHIVE): $(OBJECTS)
	ar rv $(ARCHIVE) $(OBJECTS)

%.o: %.c
	gcc -c $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ $<

clean:
	rm -f $(ARCHIVE) $(OBJECTS)

distclean: clean
