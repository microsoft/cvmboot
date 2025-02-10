TOP=$(abspath ..)

.PHONY: roothash

TARGET = cvmsign

SOURCES += $(wildcard *.c)

OBJECTS = $(SOURCES:.c=.o)

CFLAGS += -g -O3 -Wall -Werror

DEFINES =

INCLUDES =
INCLUDES += -I$(TOP)
INCLUDES += -I$(TOP)/third-party/install/include

LDFLAGS =
LDFLAGS += -L$(TOP)/common -lcvmbootcommon
LDFLAGS += -L$(TOP)/utils -lcvmbootutils
LDFLAGS += $(TOP)/third-party/install/lib64/libcrypto.a

$(TARGET): $(OBJECTS)
	$(MAKE) -C .. timestamp.h
	gcc $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	cp $(TARGET) cvmsign-init
	cp $(TARGET) cvmsign-verify

%.o: %.c
	gcc -c $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ $<

clean:
	rm -rf $(TARGET) cvmsign-init cvmsign-verify $(OBJECTS) output

distclean: clean

DIRNAME=output
FILENAME=$(DIRNAME)/cvmsign

tests:
	./cvmsign-init
	@ mkdir -p $(DIRNAME)
	@ cp ./cvmsign $(FILENAME)
	./cvmsign $(FILENAME)
	./cvmsign-verify $(FILENAME) $(FILENAME).sig $(FILENAME).pub

verify:
	./cvmsign-verify $(FILENAME) $(FILENAME).sig $(FILENAME).pub

init:
	./cvmsign-init

ldd:
	ldd $(TARGET)
