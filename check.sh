#!/bin/sh
set -eu

: "${CC:=cc}"
: "${CFLAGS:=-std=c99 -Wall -Wextra -Wpedantic -O2}"

tmp=${TMPDIR:-/tmp}/ctc-check.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

cat > "$tmp/telemetry.atlas" <<'ATLAS'
telemetry renderer.frame.ms f64 ms "Frame render time"
limit renderer.frame.ms > 16.6 warn frame.slow cooldown 5s
limit renderer.frame.ms > 33.3 error frame.very_slow cooldown 5s

telemetry worker.queue.depth u32 count "Pending work items"
limit worker.queue.depth > 1000 error queue.backpressure cooldown 10s

telemetry worker.heartbeat.age f64 s "Seconds since worker heartbeat"
limit worker.heartbeat.age stale 5s error worker.heartbeat.missing cooldown 30s

command renderer.set_quality "Change render quality"
arg renderer.set_quality level enum "Quality preset: low, medium, high"

command worker.dump_state "Write worker state to a diagnostics file"
arg worker.dump_state path string "Output path"
ATLAS

cat > "$tmp/probe-target.c" <<'C'
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

volatile float imu_temperature_c = 72.0f;
volatile float battery_voltage = 12.4f;
volatile double control_loop_ms = 2.1;
volatile uint32_t dropped_packets = 0;

int main(void) {
    fprintf(stderr, "target pid=%ld\n", (long)getpid());
    fflush(stderr);

    for (int i = 0; i < 1000; ++i) {
        imu_temperature_c += 0.25f;
        battery_voltage -= 0.01f;
        control_loop_ms += 0.02;
        if (i % 17 == 0) dropped_packets++;

        struct timespec req;
        req.tv_sec = 0;
        req.tv_nsec = 50 * 1000 * 1000;
        nanosleep(&req, NULL);
    }

    return 0;
}
C

cat > "$tmp/probe-rules.trip" <<'RULES'
imu.temperature_c > 80 warn imu.overtemp
battery.voltage < 10.8 error battery.low
control.loop_ms > 4.0 error control_loop.overrun
net.dropped_packets > 0 warn net.packet_loss
RULES

./atlas check "$tmp/telemetry.atlas" > "$tmp/atlas-check.out"
grep -q 'telemetry=3 limits=4 commands=2 args=2' "$tmp/atlas-check.out"

./atlas rules "$tmp/telemetry.atlas" > "$tmp/rules.trip"
grep -q 'renderer.frame.ms .* warn   frame.slow cooldown 5s' "$tmp/rules.trip"
grep -q 'worker.heartbeat.age .* stale .* error  worker.heartbeat.missing cooldown 30s' "$tmp/rules.trip"

./atlas doc "$tmp/telemetry.atlas" > "$tmp/TELEMETRY.md"
grep -q '| renderer.frame.ms | f64 | ms | Frame render time |' "$tmp/TELEMETRY.md"

./atlas header "$tmp/telemetry.atlas" > "$tmp/telemetry_ids.h"
grep -q '#define CTC_TEL_RENDERER_FRAME_MS "renderer.frame.ms"' "$tmp/telemetry_ids.h"
grep -q '#define CTC_CMD_WORKER_DUMP_STATE "worker.dump_state"' "$tmp/telemetry_ids.h"

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

./atlas rules "$tmp/telemetry.atlas" | ./trip -r - --fail-on never > "$tmp/stdin-rules.out"
test ! -s "$tmp/stdin-rules.out"

if [ "$(uname -s)" = "Linux" ]; then
    "$CC" -std=c99 -O0 -g "$tmp/probe-target.c" -o "$tmp/probe-target"

    ./probe \
        -s imu.temperature_c:f32:imu_temperature_c \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '^imu.temperature_c='

    ./probe \
        --json \
        -s control.loop_ms:f64:control_loop_ms \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '"control.loop_ms"'

    ./probe \
        --format limlog \
        -s battery.voltage:f32:battery_voltage \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep ' key=battery.voltage '

    PATH=/no-such-path ./probe \
        -s imu.temperature_c:f32:imu_temperature_c \
        -n 1 \
        -- "$tmp/probe-target" \
        > "$tmp/no-nm.out"
    grep '^imu.temperature_c=' "$tmp/no-nm.out"

    set +e
    ./probe \
        -s net.dropped_packets:u32:dropped_packets \
        -n 1 \
        --rules "$tmp/probe-rules.trip" \
        --fail-on warn \
        -- "$tmp/probe-target" \
        > "$tmp/probe-events.out"
    status=$?
    set -e

    test "$status" -eq 1
    grep -q 'warn	net.packet_loss' "$tmp/probe-events.out"

    set +e
    ./probe -p $$ -s drone_state.imu.temperature_c:f32 > "$tmp/dwarf.out" 2> "$tmp/dwarf.err"
    status=$?
    set -e
    test "$status" -eq 2
    grep -q 'DWARF field paths are not implemented yet' "$tmp/dwarf.err"

    if command -v c++ >/dev/null 2>&1; then
        cat > "$tmp/cpp-target.cpp" <<'CPP'
#include <unistd.h>

namespace drone {
volatile float cpp_temperature_c = 91.25f;
}

int main() {
    for (int i = 0; i < 100; ++i) {
        drone::cpp_temperature_c += 0.25f;
        usleep(50000);
    }
    return 0;
}
CPP
        c++ -O0 -g "$tmp/cpp-target.cpp" -o "$tmp/cpp-target"
        ./probe \
            -s cpp.temperature:f32:drone::cpp_temperature_c \
            -n 1 \
            -- "$tmp/cpp-target" \
            | grep '^cpp.temperature='
    fi
else
    echo "check: probe runtime checks skipped on non-Linux"
fi

cat > "$tmp/test_dwell.c" <<'C'
#include "ctc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct test_events {
    unsigned long count;
    char last[CTC_LINE_MAX];
} test_events;

static void on_event(const char *line, void *user) {
    test_events *events = (test_events *)user;
    events->count++;
    snprintf(events->last, sizeof(events->last), "%s", line ? line : "");
}

int main(void) {
    ctc_dwell_ctx ctx;
    test_events events;
    volatile uint32_t queue_depth = 1201;
    volatile double frame_ms = 12.5;
    int rc;

    memset(&events, 0, sizeof(events));
    ctc_dwell_init(&ctx);
    ctc_dwell_set_event_callback(&ctx, on_event, &events);

    if (ctc_dwell_add_rule(&ctx,
                           "worker.queue.depth",
                           CTC_GT,
                           1000.0,
                           CTC_ERROR,
                           "queue.backpressure") != 0) {
        return 1;
    }
    if (ctc_dwell_add_rule(&ctx,
                           "renderer.frame.ms",
                           CTC_GT,
                           16.6,
                           CTC_WARN,
                           "frame.slow") != 0) {
        return 2;
    }

    if (ctc_dwell_watch_u32(&ctx,
                            "worker.queue.depth",
                            &queue_depth) != 0) {
        return 3;
    }
    if (ctc_dwell_watch_f64(&ctx,
                            "renderer.frame.ms",
                            &frame_ms) != 0) {
        return 4;
    }

    rc = ctc_dwell_tick(&ctx);
    if (rc != 1) return 5;
    if (events.count != 1) return 6;
    if (strstr(events.last, "queue.backpressure") == NULL) return 7;
    if (ctx.contracts.error_count != 1) return 8;
    if (ctx.contracts.warn_count != 0) return 9;

    frame_ms = 20.0;
    rc = ctc_dwell_tick(&ctx);
    if (rc != 2) return 10;
    if (events.count != 3) return 11;
    if (ctx.contracts.error_count != 2) return 12;
    if (ctx.contracts.warn_count != 1) return 13;

    return 0;
}
C

cat > "$tmp/embedded-main.c" <<'C'
#include "ctc.h"
#include "telemetry_ids.h"

#include <stdint.h>
#include <stdio.h>

static void print_event(const char *line, void *user) {
    (void)user;
    puts(line);
}

int main(void) {
    ctc_dwell_ctx ctx;
    volatile uint32_t queue_depth = 1201;
    int emitted;

    ctc_dwell_init(&ctx);
    ctc_dwell_set_event_callback(&ctx, print_event, NULL);

    if (ctc_dwell_add_rule(&ctx,
                           CTC_TEL_WORKER_QUEUE_DEPTH,
                           CTC_GT,
                           1000.0,
                           CTC_ERROR,
                           "queue.backpressure") != 0) {
        return 1;
    }

    if (ctc_dwell_watch_u32(&ctx,
                            CTC_TEL_WORKER_QUEUE_DEPTH,
                            &queue_depth) != 0) {
        return 1;
    }

    emitted = ctc_dwell_tick(&ctx);
    return emitted == 1 ? 0 : 1;
}
C

"$CC" $CFLAGS -Iinclude "$tmp/test_dwell.c" libctc.a -o "$tmp/test_dwell"
"$tmp/test_dwell"

"$CC" $CFLAGS -Iinclude -I"$tmp" "$tmp/embedded-main.c" libctc.a -o "$tmp/ctc_embedded_example"
"$tmp/ctc_embedded_example" > "$tmp/ctc_embedded_example.out"
