TOP=$(abspath ..)

all: build

TARGET = ./sparsefs-mount

CFLAGS =
CFLAGS += -Wall -Werror -O3 -g
CFLAGS += $(shell pkg-config fuse3 --cflags)
CFLAGS += -D_FILE_OFFSET_BITS=64

INCLUDES = -I$(TOP)

LDFLAGS =
LDFLAGS += -L$(TOP)/common -lcvmbootcommon
LDFLAGS += $(shell pkg-config fuse3 --libs)
LDFLAGS += -lcrypto

SOURCES = sparsefs.c

ifdef TRACE
TRACEOPT=-t
endif

build: $(SOURCES)
	gcc $(CFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCES) $(LDFLAGS)

mount:
	$(TARGET) -f $(TRACEOPT) basedir mntdir

umount:
	fusermount -u ./mntdir

clean:
	rm -rf $(TARGET)

distclean: clean

ldd:
	ldd $(TARGET)
