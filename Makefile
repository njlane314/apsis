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

WRAPPERS = apsis
TOOLS = trip dwell atlas probe bind

all: $(WRAPPERS) libapsis.a $(TOOLS)

lib/contract.o: lib/contract.c include/apsis_contract.h
	$(CC) $(CFLAGS) -Iinclude -c lib/contract.c -o lib/contract.o

lib/dwell.o: lib/dwell.c include/apsis_contract.h include/apsis_dwell.h
	$(CC) $(CFLAGS) -Iinclude -c lib/dwell.c -o lib/dwell.o

libapsis.a: $(LIB_OBJS)
	$(AR) rcs libapsis.a $(LIB_OBJS)

trip: tools/trip.c include/apsis.h include/apsis_contract.h libapsis.a
	$(CC) $(CFLAGS) -Iinclude tools/trip.c libapsis.a -o trip

dwell: tools/dwell.c include/apsis.h include/apsis_dwell.h libapsis.a
	$(CC) $(CFLAGS) -Iinclude tools/dwell.c libapsis.a -o dwell

atlas: tools/atlas.c
	$(CC) $(CFLAGS) tools/atlas.c -o atlas

probe: tools/probe.c include/apsis.h include/apsis_contract.h libapsis.a
	$(CC) $(CFLAGS) -Iinclude tools/probe.c libapsis.a -o probe

bind: tools/bind.c include/apsis.h include/apsis_contract.h libapsis.a
	$(CC) $(CFLAGS) -Iinclude tools/bind.c libapsis.a -o bind

check: all
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./check.sh

demo-probe: all
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./scripts/demo-probe.sh

guardrail-scan:
	@command -v rg >/dev/null 2>&1 || { echo "guardrail-scan: rg is required"; exit 2; }
	@! rg -n '\b(malloc|calloc|realloc|free)\s*\(' --glob '*.c' --glob '*.h'
	@! rg -n '\b(goto|setjmp|longjmp)\b' --glob '*.c' --glob '*.h'
	@echo "guardrail-scan: ok"

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(LIBDIR)
	cp apsis trip dwell atlas probe bind $(DESTDIR)$(BINDIR)/
	cp include/*.h $(DESTDIR)$(INCLUDEDIR)/
	cp libapsis.a $(DESTDIR)$(LIBDIR)/

clean:
	rm -f $(LIB_OBJS) libapsis.a $(TOOLS)

.PHONY: all check demo-probe guardrail-scan install clean
