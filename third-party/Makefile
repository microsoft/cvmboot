TOP=$(abspath ..)
include $(TOP)/defs.mak

DIRS = gnu-efi openssl libcrc

all:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) $(NL) )

clean:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) clean $(NL) )

distclean:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) distclean $(NL) )

