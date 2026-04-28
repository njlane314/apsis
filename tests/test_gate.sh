#!/bin/sh
set -eu

: "${CC:=cc}"
: "${CFLAGS:=-std=c99 -Wall -Wextra -Wpedantic -O2}"

repo_dir=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp=${TMPDIR:-/tmp}/apsis-gate-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

cd "$repo_dir"

cat > "$tmp/clean.atlas" <<'ATLAS'
telemetry metric.beta f32 units "Secondary metric"
limit metric.beta < 1 error metric.beta.low cooldown 1s
ATLAS

set +e
./gate --atlas "$tmp/missing.atlas" --binary ./gate -- ./gate --help \
    > "$tmp/missing-atlas.out" 2> "$tmp/missing-atlas.err"
status=$?
set -e
test "$status" -eq 2
grep -q 'gate: atlas failed' "$tmp/missing-atlas.err"

set +e
./gate --atlas "$tmp/clean.atlas" --binary "$tmp/no-such-binary" \
    -- "$tmp/no-such-binary" > "$tmp/missing-binary.out" 2> "$tmp/missing-binary.err"
status=$?
set -e
test "$status" -eq 2
grep -q 'gate: binary path not readable' "$tmp/missing-binary.err"

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_gate: runtime checks skipped on non-Linux"
    exit 0
fi

cat > "$tmp/gate-target.c" <<'C'
#define _POSIX_C_SOURCE 200809L
#include <time.h>

volatile float metric_alpha = 72.0f;
volatile float metric_beta = 12.4f;

int main(void) {
    struct timespec req;

    req.tv_sec = 2;
    req.tv_nsec = 0;
    nanosleep(&req, NULL);
    return 0;
}
C

cat > "$tmp/violation.atlas" <<'ATLAS'
telemetry metric.alpha f32 units "Primary metric"
limit metric.alpha > 70 error metric.alpha.high cooldown 1s
ATLAS

"$CC" $CFLAGS "$tmp/gate-target.c" -o "$tmp/gate-target"

./gate --atlas "$tmp/clean.atlas" \
    --binary "$tmp/gate-target" \
    --count 1 \
    --interval 10ms \
    --fail-on error \
    -- "$tmp/gate-target" \
    > "$tmp/clean.out" 2> "$tmp/clean.err"
test ! -s "$tmp/clean.out"

./gate --explain \
    --atlas "$tmp/clean.atlas" \
    --binary "$tmp/gate-target" \
    --count 1 \
    --interval 10ms \
    --fail-on never \
    -- "$tmp/gate-target" \
    > "$tmp/explain.out" 2> "$tmp/explain.err"
grep -q '^gate plan:' "$tmp/explain.err"
grep -q 'bind emit watch .* --verify-types' "$tmp/explain.err"

set +e
./gate --atlas "$tmp/violation.atlas" \
    --binary "$tmp/gate-target" \
    --count 1 \
    --interval 10ms \
    --fail-on error \
    -- "$tmp/gate-target" \
    > "$tmp/violation.out" 2> "$tmp/violation.err"
status=$?
set -e
test "$status" -eq 1
grep -q 'error	metric.alpha.high' "$tmp/violation.out"

./gate --atlas "$tmp/violation.atlas" \
    --binary "$tmp/gate-target" \
    --count 1 \
    --interval 10ms \
    --fail-on never \
    --format github \
    --summary-file "$tmp/summary.md" \
    -- "$tmp/gate-target" \
    > "$tmp/github.out" 2> "$tmp/github.err"
grep -q '### apsis trip summary' "$tmp/summary.md"

./gate --atlas "$tmp/violation.atlas" \
    --binary "$tmp/gate-target" \
    --count 1 \
    --interval 10ms \
    --fail-on never \
    --format jsonl \
    -- "$tmp/gate-target" \
    > "$tmp/jsonl.out" 2> "$tmp/jsonl.err"
grep -q '"event_id":"metric.alpha.high"' "$tmp/jsonl.out"
