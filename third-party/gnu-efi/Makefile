.PHONY: checkout 

TARGET1=./$(DIRNAME)/x86_64/lib/libefi.a
TARGET2=./$(DIRNAME)/x86_64/gnuefi/libgnuefi.a
VERSION=$(shell cat VERSION)
DIRNAME=gnu-efi-$(VERSION)
TARFILE=$(DIRNAME).tar
BZ2FILE=$(TARFILE).bz2

all:
	$(MAKE) checkout
	$(MAKE) depend
	$(MAKE) build

build: $(TARGET1) $(TARGET2)

$(TARGET1):
	( cd $(DIRNAME); make lib )

$(TARGET2):
	( cd $(DIRNAME); make gnuefi )
	$(MAKE) lds

LINKSCRIPT=elf_x86_64_efi.lds

clean:
	mkdir -p $(DIRNAME)
	- ( cd $(DIRNAME); make clean 2> /dev/null )

distclean: clean
	rm -rf $(DIRNAME)
	rm -rf $(LINKSCRIPT)
	rm -f $(BZ2FILE) $(TARFILE) ./download
	rm -f depend.mak

checkout: ./$(DIRNAME)/Makefile

$(DIRNAME)/Makefile:
	rm -f $(BZ2FILE) $(TARFILE) ./download
	wget https://sourceforge.net/projects/gnu-efi/files/$(BZ2FILE)/download
	mv download $(BZ2FILE)
	bunzip2 $(BZ2FILE)
	tar xvf $(TARFILE)
	rm -f $(BZ2FILE) $(TARFILE) ./download
	$(MAKE) patch

INIT_C=./gnu-efi-3.0.14/lib/init.c

patch:
	cp $(INIT_C) $(INIT_C).tmp
	sed -f patch.sed $(INIT_C).tmp > $(INIT_C)

lds:
	sed -f sub.sed ./$(DIRNAME)/gnuefi/elf_x86_64_efi.lds > $(LINKSCRIPT)

depend:
	@ rm -f depend.mak
	@ echo "$(TARGET1): \\" >> depend.mak
	@ find . -name '*.[ch]' | sed 's/.*/& \\/g' >> depend.mak
	@ echo "" >> depend.mak
	@ echo "$(TARGET2): \\" >> depend.mak
	@ find . -name '*.[ch]' | sed 's/.*/& \\/g' >> depend.mak
	@ echo "" >> depend.mak

-include depend.mak
