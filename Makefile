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
	tools/contract.o \
	tools/dwell_lib.o

WRAPPERS = apsis
TOOLS = trip dwell atlas probe bind gate bound

all: $(WRAPPERS) libapsis.a $(TOOLS)

tools/contract.o: tools/contract.c include/apsis_contract.h
	$(CC) $(CFLAGS) -Iinclude -c tools/contract.c -o tools/contract.o

tools/dwell_lib.o: tools/dwell_lib.c include/apsis_contract.h include/apsis_dwell.h
	$(CC) $(CFLAGS) -Iinclude -c tools/dwell_lib.c -o tools/dwell_lib.o

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

gate: tools/gate/gate.c
	$(CC) $(CFLAGS) tools/gate/gate.c -o gate

bound: tools/bound/bound.c
	$(CC) $(CFLAGS) tools/bound/bound.c -o bound

check: all
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./check.sh
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./tests/test_gate.sh
	CC="$(CC)" CFLAGS="$(CFLAGS)" sh ./tests/test_bound.sh

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
	cp include/*.h $(DESTDIR)$(INCLUDEDIR)/
	cp libapsis.a $(DESTDIR)$(LIBDIR)/
	cp man/*.1 $(DESTDIR)$(MANDIR)/man1/
	cp man/*.7 $(DESTDIR)$(MANDIR)/man7/

uninstall:
	for file in $(WRAPPERS) $(TOOLS); do \
	    rm -f "$(DESTDIR)$(BINDIR)/$$file"; \
	done
	for file in include/*.h; do \
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
