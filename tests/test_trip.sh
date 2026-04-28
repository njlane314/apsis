#!/bin/sh
set -eu

tmp=${TMPDIR:-/tmp}/ctc-trip-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

./atlas rules examples/rover/telemetry.atlas > "$tmp/rules.trip"

cat > "$tmp/samples.tlm" <<'SAMPLES'
renderer.frame.ms=18.7
worker.queue.depth 1402
worker.heartbeat.age=6
SAMPLES

set +e
./trip -r "$tmp/rules.trip" --summary < "$tmp/samples.tlm" > "$tmp/events.out"
status=$?
set -e

test "$status" -eq 1
grep -q 'warn	frame.slow	renderer.frame.ms	>' "$tmp/events.out"
grep -q 'error	queue.backpressure	worker.queue.depth	>' "$tmp/events.out"
grep -q 'error	worker.heartbeat.missing	worker.heartbeat.age	stale' "$tmp/events.out"

./atlas rules examples/rover/telemetry.atlas | ./trip -r - --fail-on never > "$tmp/stdin-rules.out"
test ! -s "$tmp/stdin-rules.out"
