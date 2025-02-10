TOP=$(abspath .)
.PHONY: prereqs
.PHONY: update
.PHONY: tests
.PHONY: bindist
.PHONY: install
include $(TOP)/defs.mak

DIRS = third-party cencode libc utils common bootloader cvmdisk cvmvhd cvmsign akvsign sparsefs tests azcopy

all: .prereqs
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) $(NL) )

clean:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) clean $(NL) )
	@ rm -rf timestamp.h
	@ sudo rm -rf bindist $(PACKAGE) $(PACKAGE).tar.gz

distclean:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) distclean $(NL) )
	@ rm -rf .prereqs

tests:
	@ $(MAKE) -s -C cvmsign tests

.prereqs:
	./prereqs/install.sh
	mkdir -p .prereqs

update:
	$(MAKE) -C update update

PREFIX=$(DESTDIR)/
BINDIR=$(PREFIX)/usr/bin
SHAREDIR=$(PREFIX)/usr/share/cvmboot

INSTALL=sudo install -D

install:
	$(MAKE) uninstall
	$(INSTALL) sparsefs/sparsefs-mount $(BINDIR)/sparsefs-mount
	$(INSTALL) cvmdisk/cvmdisk $(BINDIR)/cvmdisk
	$(INSTALL) cvmsign/cvmsign $(BINDIR)/cvmsign
	$(INSTALL) cvmsign/cvmsign-init $(BINDIR)/cvmsign-init
	$(INSTALL) cvmsign/cvmsign-verify $(BINDIR)/cvmsign-verify
	$(INSTALL) bootloader/cvmboot.efi $(SHAREDIR)/cvmboot.efi
	$(INSTALL) akvsign/target/release/akvsign $(BINDIR)/akvsign
	$(INSTALL) cvmvhd/cvmvhd $(BINDIR)/cvmvhd
	$(INSTALL) azcopy/azcopy $(BINDIR)/azcopy
	sudo mkdir -p $(SHAREDIR)
	sudo rm -rf $(SHAREDIR)
	sudo cp -r share/cvmboot $(SHAREDIR)

uninstall:
	sudo rm -rf $(BINDIR)/cvmdisk
	sudo rm -rf $(BINDIR)/cvmsign
	sudo rm -rf $(BINDIR)/akvsign
	sudo rm -rf $(BINDIR)/cvmsign-init
	sudo rm -rf $(BINDIR)/cvmvhd
	sudo rm -rf $(SHAREDIR)

check-install:
	cmp cvmdisk/cvmdisk $(BINDIR)/cvmdisk
	cmp bootloader/cvmboot.efi $(SHAREDIR)/cvmboot.efi
	cmp cvmsign/cvmsign $(BINDIR)/cvmsign
	cmp cvmsign/cvmsign-init $(BINDIR)/cvmsign-init
	cmp cvmsign/cvmsign-verify $(BINDIR)/cvmsign-verify
	cmp akvsign/target/release/akvsign $(BINDIR)/akvsign
	cmp cvmvhd/cvmvhd $(BINDIR)/cvmvhd
	diff -r share/cvmboot $(SHAREDIR)

init:
	$(MAKE) -C cvmdisk init

world:
	$(MAKE) clean
	$(MAKE)
	$(MAKE) install

timestamp.h:
	@git show --no-patch --format='%ci %H' `git log | head -1 | awk '{print $$2}'` | sed 's/.*/#define TIMESTAMP \"__timestamp__: &\"/g' > timestamp.h
	@echo created "timestamp.h"

PACKAGE=cvmboot-$(shell cat version)

bindist:
	sudo rm -rf $(PACKAGE)
	$(MAKE) install DESTDIR=$(PACKAGE)
	sudo cp install/install.sh $(PACKAGE)
	tar cfz $(PACKAGE).tar.gz $(PACKAGE)
	sudo rm -rf $(PACKAGE)
	@ echo "Created $(PACKAGE)"
