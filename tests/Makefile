.PHONY: all clean distclean
include ../defs.mak

DIRS += events

all:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) $(NL) )

clean:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) clean $(NL) )

distclean:
	@ $(foreach i, $(DIRS), $(MAKE) -C $(i) distclean $(NL) )

tests:
	@ $(foreach i, $(DIRS), $(MAKE) -s -C $(i) tests $(NL) )
