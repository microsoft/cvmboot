PREFIX=$(shell realpath ../install)

all:
	$(MAKE) openssl
	$(MAKE) openssl/Makefile
	$(MAKE) .build
	$(MAKE) .install

VERSION=openssl-3.0

openssl:
	git clone -b $(VERSION) https://github.com/openssl/openssl

#openssl/Makefile:
#	( cd openssl; ./config -static --prefix=$(PREFIX) )

opts=linux-x86_64 no-shared no-asm

openssl/Makefile:
	( cd openssl; ./Configure $(opts) -static --prefix=$(PREFIX) )

.build:
	$(MAKE) -C openssl
	touch .build

.install:
	$(MAKE) -C openssl install
	touch .install

clean:

distclean:
	rm -rf openssl .build .install ../install
