TOP=$(abspath ..)

TARGET=libcvmbootcommon.a
SOURCES += $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS += -g -O3 -Wall -Werror
DEFINES =
INCLUDES = -I$(TOP) -I$(TOP)/third-party/install/include
LDFLAGS =

$(TARGET): $(OBJECTS)
	ar rv $(TARGET) $(OBJECTS)

%.o: %.c
	gcc -c $(CFLAGS) $(DEFINES) $(INCLUDES) -o $@ $<

clean:
	rm -rf $(OBJECTS) $(TARGET)

distclean: clean
