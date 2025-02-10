
all:
	$(MAKE) libcrc
	$(MAKE) -C libcrc
	$(MAKE) install

libcrc:
	git clone https://github.com/lammertb/libcrc

clean:
	rm -rf libcrc

distclean: clean

INSTALL=install -D

install:
	$(INSTALL) ./libcrc/include/checksum.h ../install/include/checksum.h
	$(INSTALL) ./libcrc/lib/libcrc.a ../install/lib/libcrc.a
	touch .install

uninstall:
	rm -f ../install/include/checksum.h
	rm -f ../install/lib/libcrc.a
