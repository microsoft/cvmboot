TOP=$(abspath ..)
TARGET=cvmvhd

.PHONY: dump

CFLAGS= -Werror -Wall
INCLUDES = -I$(TOP)
LDFLAGS += -L$(TOP)/common -lcvmbootcommon

all:
	gcc $(CFLAGS) $(INCLUDES) -o $(TARGET) main.c $(LDFLAGS)

clean:
	rm -f $(TARGET) sample.vhd.gz sample.vhd image

install:
	sudo cp cvmvhd /usr/bin/cvmvhd

uninstall:
	sudo rm -rf /usr/bin/cvmvhd

distclean: clean

dump:
	./$(TARGET) dump image

append: sample.vhd
	cp --sparse=auto sample.vhd image
	./$(TARGET) append image
	truncate --size=-512 image
	./$(TARGET) append image
	./$(TARGET) dump image

remove:
	./$(TARGET) remove image

sample.vhd:
	cp ../share/cvmboot/sample.vhd.gz sample.vhd.gz
	gzip -d sample.vhd.gz

tests:
	$(MAKE) sample.vhd
	$(MAKE) append
	$(MAKE) remove

ldd:
	ldd $(TARGET)
