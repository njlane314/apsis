CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
PREFIX ?= /usr/local

.PHONY: all clean test install

all: trip

trip: trip.c trip.h
	$(CC) $(CFLAGS) trip.c -o trip

test: trip
	@tmp=$$(mktemp -d); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf '%s\n' \
		'frame.ms > 16.6 warn frame.slow' \
		'queue.depth > 1000 error queue.backpressure' \
		'heartbeat stale 5s error heartbeat.missing' \
		'temperature > 80 warn temperature.high cooldown 10s' \
		> "$$tmp/rules.trip"; \
	printf '%s\n' \
		'frame.ms=18.7' \
		'queue.depth=1402' \
		'heartbeat=6' \
		'temperature=82' \
		'temperature=83' \
		> "$$tmp/sample.tlm"; \
	set +e; \
	./trip -r "$$tmp/rules.trip" --summary < "$$tmp/sample.tlm" > "$$tmp/events.out"; \
	status=$$?; \
	set -e; \
	cat "$$tmp/events.out"; \
	test "$$status" -eq 1; \
	grep -q 'warn	frame.slow' "$$tmp/events.out"; \
	grep -q 'error	queue.backpressure' "$$tmp/events.out"; \
	grep -q 'error	heartbeat.missing' "$$tmp/events.out"; \
	test "$$(grep -c 'warn	temperature.high' "$$tmp/events.out")" -eq 1; \
	printf '%s\n' 'heartbeat stale 5s error heartbeat.missing' > "$$tmp/stale-rules.trip"; \
	printf '%s\n' 'other.metric=1' > "$$tmp/no-heartbeat.tlm"; \
	set +e; \
	./trip -r "$$tmp/stale-rules.trip" --summary < "$$tmp/no-heartbeat.tlm" > "$$tmp/stale-events.out"; \
	stale_status=$$?; \
	set -e; \
	cat "$$tmp/stale-events.out"; \
	test "$$stale_status" -eq 1; \
	grep -q 'error	heartbeat.missing' "$$tmp/stale-events.out"

install: trip
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 trip $(DESTDIR)$(PREFIX)/bin/trip

clean:
	rm -f trip *.o
