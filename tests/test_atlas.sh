#!/bin/sh
set -eu

tmp=${TMPDIR:-/tmp}/ctc-atlas-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

./atlas check examples/rover/telemetry.atlas > "$tmp/check.out"
grep -q 'telemetry=3 limits=4 commands=2 args=2' "$tmp/check.out"

./atlas rules examples/rover/telemetry.atlas > "$tmp/rules.trip"
grep -q 'renderer.frame.ms .* warn   frame.slow cooldown 5s' "$tmp/rules.trip"
grep -q 'worker.heartbeat.age .* stale .* error  worker.heartbeat.missing cooldown 30s' "$tmp/rules.trip"

./atlas doc examples/rover/telemetry.atlas > "$tmp/TELEMETRY.md"
grep -q '| renderer.frame.ms | f64 | ms | Frame render time |' "$tmp/TELEMETRY.md"

./atlas header examples/rover/telemetry.atlas > "$tmp/telemetry_ids.h"
grep -q '#define CTC_TEL_RENDERER_FRAME_MS "renderer.frame.ms"' "$tmp/telemetry_ids.h"
grep -q '#define CTC_CMD_WORKER_DUMP_STATE "worker.dump_state"' "$tmp/telemetry_ids.h"
