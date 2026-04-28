#!/bin/sh
set -eu

tmp=${TMPDIR:-/tmp}/apsis-bound-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

cat > "$tmp/samples.tlm" <<'SAMPLES'
metric.alpha=10
metric.alpha=12
metric.alpha=14
metric.beta 5
metric.beta 7
ts_ms=1 key=metric.gamma type=f64 addr=0x10 value=20
{"ts_ms":2,"name":"metric.gamma","type":"f64","addr":"0x10","value":22}
SAMPLES

./bound learn "$tmp/samples.tlm" --min-samples 2 --emit report \
    > "$tmp/report.out"
grep -q '^key: metric.alpha$' "$tmp/report.out"
grep -q '^  samples: 3$' "$tmp/report.out"
grep -q '^  min: 10$' "$tmp/report.out"
grep -q '^  max: 14$' "$tmp/report.out"
grep -q '^  mean: 12$' "$tmp/report.out"

./bound learn "$tmp/samples.tlm" --min-samples 2 --emit atlas-patch \
    > "$tmp/patch.out"
grep -q '# learned by bound; review before committing' "$tmp/patch.out"
grep -q 'limit metric.alpha > 16.8 error metric.alpha.high' "$tmp/patch.out"

./bound learn "$tmp/samples.tlm" --min-samples 2 --margin 50% \
    --emit atlas-patch > "$tmp/margin.out"
grep -q 'limit metric.alpha > 21 error metric.alpha.high' "$tmp/margin.out"

./bound learn "$tmp/samples.tlm" --min-samples 4 --emit atlas-patch \
    > "$tmp/min-samples.out"
test "$(grep -c '^limit ' "$tmp/min-samples.out")" -eq 0

./bound learn "$tmp/samples.tlm" --min-samples 2 --include gamma \
    --emit json > "$tmp/include.json"
grep -q '"key":"metric.gamma"' "$tmp/include.json"
test "$(grep -c '"key":"metric.alpha"' "$tmp/include.json")" -eq 0

cat > "$tmp/malformed.tlm" <<'SAMPLES'
metric.ok=1
not a sample line
SAMPLES

./bound learn "$tmp/malformed.tlm" --min-samples 1 --emit report \
    > "$tmp/malformed.out" 2> "$tmp/malformed.err"
grep -q 'bound: .* ignoring malformed sample' "$tmp/malformed.err"
grep -q '^key: metric.ok$' "$tmp/malformed.out"
