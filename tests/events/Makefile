TOP=../..
CFLAGS=-Wall -Werror
INCLUDES=-I$(TOP)
LDFLAGS=-L$(TOP)/utils -lcvmbootutils

all:
	gcc $(CFLAGS) $(INCLUDES) -o events main.c $(LDFLAGS)

tests:
	./events events1
	./events events2

clean:
	rm -rf events

distclean: clean
