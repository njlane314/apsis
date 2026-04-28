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

trip: tools/trip.c include/ctc.h include/ctc_contract.h libctc.a
	$(CC) $(CFLAGS) -Iinclude tools/trip.c libctc.a -o trip

dwell: tools/dwell.c include/ctc.h include/ctc_dwell.h libctc.a
	$(CC) $(CFLAGS) -Iinclude tools/dwell.c libctc.a -o dwell

atlas: tools/atlas.c
	$(CC) $(CFLAGS) tools/atlas.c -o atlas

probe: tools/probe.c include/ctc.h include/ctc_contract.h libctc.a
	$(CC) $(CFLAGS) -Iinclude tools/probe.c libctc.a -o probe

check: all
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./check.sh

guardrail-scan:
	@command -v rg >/dev/null 2>&1 || { echo "guardrail-scan: rg is required"; exit 2; }
	@! rg -n '\b(malloc|calloc|realloc|free)\s*\(' --glob '*.c' --glob '*.h'
	@! rg -n '\b(goto|setjmp|longjmp)\b' --glob '*.c' --glob '*.h'
	@echo "guardrail-scan: ok"

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(LIBDIR)
	cp trip dwell atlas probe $(DESTDIR)$(BINDIR)/
	cp include/*.h $(DESTDIR)$(INCLUDEDIR)/
	cp libctc.a $(DESTDIR)$(LIBDIR)/

clean:
	rm -f $(LIB_OBJS) libctc.a $(TOOLS)

.PHONY: all check guardrail-scan install clean
