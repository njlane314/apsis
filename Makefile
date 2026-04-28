.POSIX:

CC ?= cc
AR ?= ar
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
MANDIR ?= $(PREFIX)/share/man

LIB_OBJS = \
	src/contract.o \
	src/dwell_lib.o

WRAPPERS = apsis
TOOLS = trip dwell atlas probe bind gate bound

all: $(WRAPPERS) libapsis.a $(TOOLS)

src/contract.o: src/contract.c src/apsis_contract.h
	$(CC) $(CFLAGS) -Isrc -c src/contract.c -o src/contract.o

src/dwell_lib.o: src/dwell_lib.c src/apsis_contract.h src/apsis_dwell.h
	$(CC) $(CFLAGS) -Isrc -c src/dwell_lib.c -o src/dwell_lib.o

libapsis.a: $(LIB_OBJS)
	$(AR) rcs libapsis.a $(LIB_OBJS)

trip: src/trip.c src/apsis.h src/apsis_contract.h libapsis.a
	$(CC) $(CFLAGS) -Isrc src/trip.c libapsis.a -o trip

dwell: src/dwell.c src/apsis.h src/apsis_dwell.h libapsis.a
	$(CC) $(CFLAGS) -Isrc src/dwell.c libapsis.a -o dwell

atlas: src/atlas.c
	$(CC) $(CFLAGS) src/atlas.c -o atlas

probe: src/probe.c src/apsis.h src/apsis_contract.h libapsis.a
	$(CC) $(CFLAGS) -Isrc src/probe.c libapsis.a -o probe

bind: src/bind.c src/apsis.h src/apsis_contract.h libapsis.a
	$(CC) $(CFLAGS) -Isrc src/bind.c libapsis.a -o bind

gate: src/gate.c
	$(CC) $(CFLAGS) src/gate.c -o gate

bound: src/bound.c
	$(CC) $(CFLAGS) src/bound.c -o bound

check: all
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./check.sh

guardrail-scan:
	@command -v rg >/dev/null 2>&1 || { echo "guardrail-scan: rg is required"; exit 2; }
	@! rg -n '\b(malloc|calloc|realloc|free)\s*\(' --glob '*.c' --glob '*.h'
	@! rg -n '\b(goto|setjmp|longjmp)\b' --glob '*.c' --glob '*.h'
	@echo "guardrail-scan: ok"

install: all
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(INCLUDEDIR) \
	    $(DESTDIR)$(LIBDIR) $(DESTDIR)$(MANDIR)/man1 \
	    $(DESTDIR)$(MANDIR)/man7
	cp $(WRAPPERS) $(TOOLS) $(DESTDIR)$(BINDIR)/
	cp src/apsis*.h $(DESTDIR)$(INCLUDEDIR)/
	cp libapsis.a $(DESTDIR)$(LIBDIR)/
	cp man/*.1 $(DESTDIR)$(MANDIR)/man1/
	cp man/*.7 $(DESTDIR)$(MANDIR)/man7/

uninstall:
	for file in $(WRAPPERS) $(TOOLS); do \
	    rm -f "$(DESTDIR)$(BINDIR)/$$file"; \
	done
	for file in src/apsis*.h; do \
	    rm -f "$(DESTDIR)$(INCLUDEDIR)/$$(basename "$$file")"; \
	done
	rm -f "$(DESTDIR)$(LIBDIR)/libapsis.a"
	for file in man/*.1; do \
	    rm -f "$(DESTDIR)$(MANDIR)/man1/$$(basename "$$file")"; \
	done
	for file in man/*.7; do \
	    rm -f "$(DESTDIR)$(MANDIR)/man7/$$(basename "$$file")"; \
	done

clean:
	rm -f $(LIB_OBJS) libapsis.a $(TOOLS)

.PHONY: all check guardrail-scan install uninstall clean
