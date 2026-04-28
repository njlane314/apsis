CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
PREFIX ?= /usr/local

.PHONY: all clean test install

all: dwell

dwell: dwell.c dwell.h
	$(CC) $(CFLAGS) dwell.c -o dwell

test: dwell
	@tmp=$$(mktemp -d); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf '%s\n' \
		'frame.ms > 16.6 warn frame.slow' \
		'queue.depth > 1000 error queue.backpressure' \
		'tokens.per_second < 50 error inference.too_slow' \
		'temperature > 80 warn temperature.high' \
		> "$$tmp/contracts.dwell"; \
	printf '%s\n' \
		'frame.ms=18.7' \
		'queue.depth=1402' \
		'tokens.per_second=49' \
		'temperature=82' \
		> "$$tmp/sample.tlm"; \
	set +e; \
	./dwell -r "$$tmp/contracts.dwell" --summary < "$$tmp/sample.tlm" > "$$tmp/events.out"; \
	status=$$?; \
	set -e; \
	cat "$$tmp/events.out"; \
	test "$$status" -eq 1; \
	grep -q 'warn	frame.slow' "$$tmp/events.out"; \
	grep -q 'error	queue.backpressure' "$$tmp/events.out"; \
	grep -q 'error	inference.too_slow' "$$tmp/events.out"; \
	grep -q 'warn	temperature.high' "$$tmp/events.out"

install: dwell
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 dwell $(DESTDIR)$(PREFIX)/bin/dwell

clean:
	rm -f dwell *.o
