CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O2
PREFIX ?= /usr/local

.PHONY: all clean test install

all: lim

lim: lim.c lim.h
	$(CC) $(CFLAGS) lim.c -o lim

test: lim
	@tmp=$$(mktemp -d); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	printf '%s\n' \
		'frame.ms > 16.6 warn frame.slow' \
		'queue.depth > 1000 error queue.backpressure' \
		'heartbeat stale 5s error heartbeat.missing' \
		'temperature > 80 warn temperature.high cooldown 10s' \
		> "$$tmp/rules.lim"; \
	printf '%s\n' \
		'frame.ms=18.7' \
		'queue.depth=1402' \
		'heartbeat=6' \
		'temperature=82' \
		'temperature=83' \
		> "$$tmp/sample.tlm"; \
	set +e; \
	./lim -r "$$tmp/rules.lim" --summary < "$$tmp/sample.tlm" > "$$tmp/events.jsonl"; \
	status=$$?; \
	set -e; \
	cat "$$tmp/events.jsonl"; \
	test "$$status" -eq 1; \
	grep -q '"id":"frame.slow"' "$$tmp/events.jsonl"; \
	grep -q '"id":"queue.backpressure"' "$$tmp/events.jsonl"; \
	grep -q '"id":"heartbeat.missing"' "$$tmp/events.jsonl"; \
	test "$$(grep -c '"id":"temperature.high"' "$$tmp/events.jsonl")" -eq 1; \
	printf '%s\n' 'heartbeat stale 5s error heartbeat.missing' > "$$tmp/stale-rules.lim"; \
	printf '%s\n' 'other.metric=1' > "$$tmp/no-heartbeat.tlm"; \
	set +e; \
	./lim -r "$$tmp/stale-rules.lim" --summary < "$$tmp/no-heartbeat.tlm" > "$$tmp/stale-events.jsonl"; \
	stale_status=$$?; \
	set -e; \
	cat "$$tmp/stale-events.jsonl"; \
	test "$$stale_status" -eq 1; \
	grep -q '"id":"heartbeat.missing"' "$$tmp/stale-events.jsonl"

install: lim
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 lim $(DESTDIR)$(PREFIX)/bin/lim

clean:
	rm -f lim *.o
