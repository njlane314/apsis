.POSIX:

CC ?= cc
AR ?= ar
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
MANDIR ?= $(PREFIX)/share/man
BUILDDIR ?= bin

LIB_OBJS = \
	$(BUILDDIR)/contract.o \
	$(BUILDDIR)/dwell_lib.o

WRAPPERS = apsis
TOOLS = trip dwell atlas probe bind gate bound
TOOL_BINS = \
	$(BUILDDIR)/trip \
	$(BUILDDIR)/dwell \
	$(BUILDDIR)/atlas \
	$(BUILDDIR)/probe \
	$(BUILDDIR)/bind \
	$(BUILDDIR)/gate \
	$(BUILDDIR)/bound

all: $(BUILDDIR)/libapsis.a $(TOOL_BINS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/contract.o: src/contract.c src/apsis_contract.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc -c src/contract.c -o $(BUILDDIR)/contract.o

$(BUILDDIR)/dwell_lib.o: src/dwell_lib.c src/apsis_contract.h src/apsis_dwell.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc -c src/dwell_lib.c -o $(BUILDDIR)/dwell_lib.o

$(BUILDDIR)/libapsis.a: $(LIB_OBJS)
	mkdir -p $(BUILDDIR)
	$(AR) rcs $(BUILDDIR)/libapsis.a $(LIB_OBJS)

$(BUILDDIR)/trip: src/trip.c src/apsis.h src/apsis_contract.h $(BUILDDIR)/libapsis.a
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc src/trip.c $(BUILDDIR)/libapsis.a -o $(BUILDDIR)/trip

$(BUILDDIR)/dwell: src/dwell.c src/apsis.h src/apsis_dwell.h $(BUILDDIR)/libapsis.a
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc src/dwell.c $(BUILDDIR)/libapsis.a -o $(BUILDDIR)/dwell

$(BUILDDIR)/atlas: src/atlas.c
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) src/atlas.c -o $(BUILDDIR)/atlas

$(BUILDDIR)/probe: src/probe.c src/apsis.h src/apsis_contract.h $(BUILDDIR)/libapsis.a
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc src/probe.c $(BUILDDIR)/libapsis.a -o $(BUILDDIR)/probe

$(BUILDDIR)/bind: src/bind.c src/apsis.h src/apsis_contract.h $(BUILDDIR)/libapsis.a
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc src/bind.c $(BUILDDIR)/libapsis.a -o $(BUILDDIR)/bind

$(BUILDDIR)/gate: src/gate.c
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) src/gate.c -o $(BUILDDIR)/gate

$(BUILDDIR)/bound: src/bound.c
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) src/bound.c -o $(BUILDDIR)/bound

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
	cp $(WRAPPERS) $(DESTDIR)$(BINDIR)/
	cp $(TOOL_BINS) $(DESTDIR)$(BINDIR)/
	cp src/apsis*.h $(DESTDIR)$(INCLUDEDIR)/
	cp $(BUILDDIR)/libapsis.a $(DESTDIR)$(LIBDIR)/
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
	rm -rf $(BUILDDIR)
	rm -f src/contract.o src/dwell_lib.o libapsis.a $(TOOLS)

.PHONY: all check guardrail-scan install uninstall clean
