.POSIX:

CC ?= cc
AR ?= ar
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib

LIB_OBJS = \
	lib/contract.o \
	lib/dwell.o

TOOLS = trip dwell atlas probe

all: libctc.a $(TOOLS)

lib/contract.o: lib/contract.c include/ctc_contract.h
	$(CC) $(CFLAGS) -Iinclude -c lib/contract.c -o lib/contract.o

lib/dwell.o: lib/dwell.c include/ctc_contract.h include/ctc_dwell.h
	$(CC) $(CFLAGS) -Iinclude -c lib/dwell.c -o lib/dwell.o

libctc.a: $(LIB_OBJS)
	$(AR) rcs libctc.a $(LIB_OBJS)

trip: tools/trip/trip.c include/ctc.h include/ctc_contract.h libctc.a
	$(CC) $(CFLAGS) -Iinclude tools/trip/trip.c libctc.a -o trip

dwell: tools/dwell/dwell.c include/ctc.h include/ctc_dwell.h libctc.a
	$(CC) $(CFLAGS) -Iinclude tools/dwell/dwell.c libctc.a -o dwell

atlas: tools/atlas/atlas.c
	$(CC) $(CFLAGS) tools/atlas/atlas.c -o atlas

probe: tools/probe/probe.c include/ctc.h include/ctc_contract.h libctc.a
	$(CC) $(CFLAGS) -Iinclude tools/probe/probe.c libctc.a -o probe

check: all
	sh tests/test_atlas.sh
	sh tests/test_trip.sh
	sh tests/test_probe.sh
	$(CC) $(CFLAGS) -Iinclude tests/test_dwell.c libctc.a -o /tmp/test_dwell
	/tmp/test_dwell
	$(CC) $(CFLAGS) -Iinclude -Iexamples/rover examples/embedded/main.c libctc.a -o /tmp/ctc_embedded_example
	/tmp/ctc_embedded_example >/tmp/ctc_embedded_example.out

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(LIBDIR)
	cp trip dwell atlas probe $(DESTDIR)$(BINDIR)/
	cp include/*.h $(DESTDIR)$(INCLUDEDIR)/
	cp libctc.a $(DESTDIR)$(LIBDIR)/

clean:
	rm -f $(LIB_OBJS) libctc.a $(TOOLS)

.PHONY: all check install clean
