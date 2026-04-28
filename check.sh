#!/bin/sh
set -eu

: "${CC:=cc}"
: "${CFLAGS:=-std=c99 -Wall -Wextra -Wpedantic -O2}"

tmp=${TMPDIR:-/tmp}/apsis-check.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
repo_dir=$(pwd)

layout_files="$tmp/layout-files"
: > "$layout_files"
for path in README.md Makefile apsis; do
    if [ -f "$path" ]; then
        printf '%s\n' "$path" >> "$layout_files"
    fi
done
for dir in docs include man tests tools; do
    if [ -d "$dir" ]; then
        find "$dir" -type f -print >> "$layout_files"
    fi
done
while IFS= read -r path; do
    awk '
        length($0) > 120 {
            printf "%s:%d: line too long (%d)\n", FILENAME, FNR, length($0)
            bad = 1
        }
        END { exit bad ? 1 : 0 }
    ' "$path"
done < "$layout_files"

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
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

volatile float imu_temperature_c = 72.0f;
volatile float battery_voltage = 12.4f;
volatile double control_loop_ms = 2.1;
volatile double nonfinite_value = NAN;
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

cat > "$tmp/probe.atlas" <<'ATLAS'
telemetry imu.temperature_c f32 C "IMU temperature"
telemetry battery.voltage f32 V "Battery voltage"
telemetry control.loop_ms f64 ms "Control loop runtime"
ATLAS

cat > "$tmp/telemetry.bind" <<'BIND'
source motor.temperature f32 symbol motor_state.temperature_c
source imu.temperature_c f32 addr 0x7ffd1234
source cpp.temperature f32 symbol drone::cpp_temperature_c object ./libdrone.so
BIND

cat > "$tmp/missing-type.bind" <<'BIND'
source no.type symbol no_type_symbol
BIND

./atlas check "$tmp/telemetry.atlas" > "$tmp/atlas-check.out"
grep -q 'telemetry=3 limits=4 commands=2 args=2' "$tmp/atlas-check.out"

./apsis atlas check "$tmp/telemetry.atlas" > "$tmp/apsis-atlas-check.out"
grep -q 'telemetry=3 limits=4 commands=2 args=2' "$tmp/apsis-atlas-check.out"

./atlas emit rules "$tmp/telemetry.atlas" > "$tmp/rules.trip"
grep -q 'renderer.frame.ms .* warn   frame.slow cooldown 5s' "$tmp/rules.trip"
grep -q 'worker.heartbeat.age .* stale .* error  worker.heartbeat.missing cooldown 30s' "$tmp/rules.trip"

./apsis emit rules "$tmp/telemetry.atlas" > "$tmp/apsis-rules.trip"
cmp "$tmp/rules.trip" "$tmp/apsis-rules.trip"

./atlas emit doc "$tmp/telemetry.atlas" > "$tmp/TELEMETRY.md"
grep -q '| renderer.frame.ms | f64 | ms | Frame render time |' "$tmp/TELEMETRY.md"

./atlas emit header "$tmp/telemetry.atlas" > "$tmp/telemetry_ids.h"
grep -q '#define APSIS_TEL_RENDERER_FRAME_MS "renderer.frame.ms"' "$tmp/telemetry_ids.h"
grep -q '#define APSIS_CMD_WORKER_DUMP_STATE "worker.dump_state"' "$tmp/telemetry_ids.h"

./bind check "$tmp/telemetry.bind" > "$tmp/bind-check.out"
grep -q 'sources=3 symbols=2 addrs=1 missing_types=0' "$tmp/bind-check.out"

./bind emit watch ./drone_sim "$tmp/probe.atlas" > "$tmp/bind-watch.out"
grep -q -- '^--watch imu.temperature_c=f32@symbol:imu_temperature_c@object:./drone_sim$' \
    "$tmp/bind-watch.out"
grep -q -- '^--watch control.loop_ms=f64@symbol:control_loop_ms@object:./drone_sim$' \
    "$tmp/bind-watch.out"

./apsis bind check "$tmp/telemetry.bind" > "$tmp/apsis-bind-check.out"
grep -q 'sources=3 symbols=2 addrs=1 missing_types=0' "$tmp/apsis-bind-check.out"

./apsis doctor > "$tmp/apsis-doctor.out"
grep -q '^apsis doctor' "$tmp/apsis-doctor.out"
grep -q 'ok  atlas' "$tmp/apsis-doctor.out"

mkdir "$tmp/init-profile"
(
    cd "$tmp/init-profile"
    "$repo_dir/apsis" init --profile probe > init.out
    test -f telemetry.atlas
    test -f rules.trip
    test -f telemetry.bind
    grep -q 'metric.alpha' telemetry.atlas
    grep -q 'metric.beta' rules.trip
    grep -q 'source metric.alpha f32 symbol' telemetry.bind
)

./bind probe "$tmp/telemetry.bind" --object ./program > "$tmp/bind-probe.out"
grep -q -- '^-s motor.temperature:f32:motor_state.temperature_c@./program$' "$tmp/bind-probe.out"
grep -q -- '^-w imu.temperature_c:f32:0x7ffd1234$' "$tmp/bind-probe.out"
grep -q -- '^-s cpp.temperature:f32:drone::cpp_temperature_c@./libdrone.so$' "$tmp/bind-probe.out"

./bind json "$tmp/telemetry.bind" > "$tmp/bind.json"
grep -q '"source":"cpp.temperature"' "$tmp/bind.json"
grep -q '"object":"./libdrone.so"' "$tmp/bind.json"

./bind github "$tmp/telemetry.bind" > "$tmp/bind.md"
grep -q '| `imu.temperature_c` | `f32` | `addr` | `0x7ffd1234` | `-` |' "$tmp/bind.md"

./bind check "$tmp/missing-type.bind" > "$tmp/bind-missing-type.out"
grep -q 'missing_types=1' "$tmp/bind-missing-type.out"

set +e
./bind probe "$tmp/missing-type.bind" > "$tmp/bind-missing-type-probe.out" 2> "$tmp/bind-missing-type.err"
status=$?
set -e
test "$status" -eq 1
grep -q 'probe output requires a type' "$tmp/bind-missing-type.err"

./probe plan \
    --watch imu.temperature_c=f32@symbol:imu_temperature_c \
    --watch battery.voltage=f32@symbol:battery_voltage \
    -- ./drone_sim \
    > "$tmp/probe-plan.out"
grep -q '^probe \\$' "$tmp/probe-plan.out"
grep -q '  -s imu.temperature_c:f32:imu_temperature_c \\$' "$tmp/probe-plan.out"
grep -q '  -s battery.voltage:f32:battery_voltage \\$' "$tmp/probe-plan.out"
grep -q '  -- ./drone_sim$' "$tmp/probe-plan.out"

./apsis plan \
    --watch imu.temperature_c=f32@symbol:imu_temperature_c \
    --watch battery.voltage=f32@symbol:battery_voltage \
    -- ./drone_sim \
    > "$tmp/apsis-probe-plan.out"
cmp "$tmp/probe-plan.out" "$tmp/apsis-probe-plan.out"

./probe plan \
    --watch imu.temperature_c=f32@addr:0x7ffd1234 \
    -- ./drone_sim \
    > "$tmp/probe-plan-addr.out"
grep -q '  -w imu.temperature_c:f32:0x7ffd1234 \\$' "$tmp/probe-plan-addr.out"

cat > "$tmp/collision.atlas" <<'ATLAS'
telemetry a.b f64 ms "A"
telemetry a-b f64 ms "B"
ATLAS

set +e
./atlas emit header "$tmp/collision.atlas" > "$tmp/collision.h" 2> "$tmp/collision.err"
status=$?
set -e
test "$status" -eq 1
grep -q 'macro id collision' "$tmp/collision.err"

cat > "$tmp/samples.tlm" <<'SAMPLES'
renderer.frame.ms=40
worker.queue.depth 1402
SAMPLES

set +e
./trip check \
    --rules "$tmp/rules.trip" \
    --summary \
    --github-summary "$tmp/github-summary.md" \
    < "$tmp/samples.tlm" > "$tmp/events.out"
status=$?
set -e

test "$status" -eq 1
grep -q '### apsis trip summary' "$tmp/github-summary.md"
grep -q '| Error | 3 |' "$tmp/github-summary.md"
grep -q 'warn	frame.slow	renderer.frame.ms	>' "$tmp/events.out"
grep -q 'error	frame.very_slow	renderer.frame.ms	>' "$tmp/events.out"
grep -q 'error	queue.backpressure	worker.queue.depth	>' "$tmp/events.out"
grep -q 'error	worker.heartbeat.missing	worker.heartbeat.age	stale' "$tmp/events.out"

./apsis trip check --rules "$tmp/rules.trip" --fail-on never \
    < "$tmp/samples.tlm" > "$tmp/apsis-events.out"
grep -q 'warn	frame.slow	renderer.frame.ms	>' "$tmp/apsis-events.out"

./atlas emit rules "$tmp/telemetry.atlas" | ./trip check --rules - --fail-on never > "$tmp/stdin-rules.out"
test ! -s "$tmp/stdin-rules.out"

if [ "$(uname -s)" = "Linux" ]; then
    "$CC" -std=c99 -O0 -g "$tmp/probe-target.c" -o "$tmp/probe-target"

    ./bind emit watch "$tmp/probe-target" "$tmp/probe.atlas" --verify-types \
        > "$tmp/bind-watch-verify.out"
    grep -q -- "^--watch imu.temperature_c=f32@symbol:imu_temperature_c@object:$tmp/probe-target$" \
        "$tmp/bind-watch-verify.out"

    ./probe plan $(./bind emit watch "$tmp/probe-target" "$tmp/probe.atlas" \
        --verify-types) -- "$tmp/probe-target" > "$tmp/bind-watch-plan.out"
    grep -q "  -s imu.temperature_c:f32:imu_temperature_c@$tmp/probe-target \\\\$" \
        "$tmp/bind-watch-plan.out"

    cat > "$tmp/probe-wrong-type.atlas" <<'ATLAS'
telemetry imu.temperature_c f64 C "IMU temperature with wrong probe type"
ATLAS

    set +e
    ./bind emit watch "$tmp/probe-target" "$tmp/probe-wrong-type.atlas" \
        --verify-types > "$tmp/bind-watch-wrong.out" 2> "$tmp/bind-watch-wrong.err"
    status=$?
    set -e
    test "$status" -eq 1
    grep -q "symbol 'imu_temperature_c' has size 4, expected 8 for f64" \
        "$tmp/bind-watch-wrong.err"

    ./probe plan \
        --watch imu.temperature_c=f32@symbol:imu_temperature_c \
        --watch battery.voltage=f32@symbol:battery_voltage \
        -- "$tmp/probe-target" \
        > "$tmp/probe-plan.out"
    grep -q '^probe \\$' "$tmp/probe-plan.out"
    grep -q '  -s imu.temperature_c:f32:imu_temperature_c \\$' "$tmp/probe-plan.out"
    grep -q '  -s battery.voltage:f32:battery_voltage \\$' "$tmp/probe-plan.out"
    grep -q "  -- $tmp/probe-target" "$tmp/probe-plan.out"

    ./probe plan \
        --watch imu.temperature_c=f32@addr:0x7ffd1234 \
        -- "$tmp/probe-target" \
        > "$tmp/probe-plan-addr.out"
    grep -q '  -w imu.temperature_c:f32:0x7ffd1234 \\$' "$tmp/probe-plan-addr.out"

    ./probe symbols "$tmp/probe-target" > "$tmp/probe-symbols.out"
    grep -q '^imu_temperature_c$' "$tmp/probe-symbols.out"
    grep -q '^battery_voltage$' "$tmp/probe-symbols.out"

    ./probe symbols "$tmp/probe-target" --filter temp > "$tmp/probe-symbols-filter.out"
    grep -q '^imu_temperature_c$' "$tmp/probe-symbols-filter.out"
    test "$(grep -c '^battery_voltage$' "$tmp/probe-symbols-filter.out")" -eq 0

    ./probe symbols "$tmp/probe-target" --filter imu_temperature_c --types \
        > "$tmp/probe-symbols-types.out"
    grep -q '^imu_temperature_c	kind=object	' "$tmp/probe-symbols-types.out"
    grep -q 'hint=i32/u32/f32' "$tmp/probe-symbols-types.out"

    ./probe symbols "$tmp/probe-target" --types > "$tmp/probe-symbols-all-types.out"
    grep -q '^battery_voltage	kind=object	' "$tmp/probe-symbols-all-types.out"

    ./probe \
        -s imu.temperature_c:f32:imu_temperature_c \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '^imu.temperature_c='

    ./probe run \
        --symbol imu.temperature_c=f32:imu_temperature_c \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '^imu.temperature_c='

    ./probe run \
        --watch imu.temperature_c=f32@symbol:imu_temperature_c \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '^imu.temperature_c='

    "$tmp/probe-target" 2> "$tmp/probe-attach-target.err" &
    attach_pid=$!
    sleep 1
    ./probe attach \
        --pid "$attach_pid" \
        --symbol imu.temperature_c=f32:imu_temperature_c \
        -n 1 \
        > "$tmp/probe-attach.out"
    kill "$attach_pid" 2>/dev/null || true
    wait "$attach_pid" 2>/dev/null || true
    grep '^imu.temperature_c=' "$tmp/probe-attach.out"

    ./probe \
        --json \
        -s control.loop_ms:f64:control_loop_ms \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '"control.loop_ms"'

    ./probe \
        --json \
        -s nonfinite.value:f64:nonfinite_value \
        -n 1 \
        -- "$tmp/probe-target" \
        | grep '"value":null,"nonfinite":"nan"'

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

    PATH="$tmp:$PATH" ./probe \
        -s imu.temperature_c:f32:imu_temperature_c \
        -n 1 \
        -- probe-target \
        | grep '^imu.temperature_c='

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

    ./probe \
        -s net.dropped_packets:u32:dropped_packets \
        -n 1 \
        --rules "$tmp/probe-rules.trip" \
        --emit both \
        --fail-on never \
        -- "$tmp/probe-target" \
        > "$tmp/probe-both.out"
    grep '^net.dropped_packets=' "$tmp/probe-both.out"
    grep -q 'warn	net.packet_loss' "$tmp/probe-both.out"

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

        ./probe symbols "$tmp/cpp-target" --demangle --filter cpp_temperature_c \
            > "$tmp/cpp-symbols.out"
        grep -q '^drone::cpp_temperature_c$' "$tmp/cpp-symbols.out"
    fi
else
    echo "check: probe runtime checks skipped on non-Linux"
fi

cat > "$tmp/test_contracts.c" <<'C'
#include "apsis.h"

#include <stdio.h>
#include <string.h>

typedef struct contract_events {
    unsigned long count;
    char first[APSIS_LINE_MAX];
    char last[APSIS_LINE_MAX];
} contract_events;

static int on_contract_event(const apsis_event *event, void *user) {
    contract_events *events = (contract_events *)user;
    char line[APSIS_LINE_MAX];

    if (apsis_format_event_record(event, line, sizeof(line)) != 0) return -1;
    if (events->count == 0) {
        snprintf(events->first, sizeof(events->first), "%s", line);
    }
    snprintf(events->last, sizeof(events->last), "%s", line);
    events->count++;
    return 0;
}

int main(void) {
    apsis_ctx ctx;
    contract_events events;
    char err[256];
    int rc;

    memset(&events, 0, sizeof(events));
    apsis_init(&ctx);
    if (apsis_add_rule(&ctx, "renderer.frame.ms", APSIS_GT, 16.6,
                     APSIS_WARN, "frame.slow") != 0) return 1;
    if (apsis_add_rule(&ctx, "renderer.frame.ms", APSIS_GT, 33.3,
                     APSIS_ERROR, "frame.very_slow") != 0) return 2;

    rc = apsis_sample_each(&ctx, "renderer.frame.ms", 40.0, 1.0,
                         on_contract_event, &events);
    if (rc != 2) return 3;
    if (events.count != 2) return 4;
    if (strstr(events.first, "frame.slow") == NULL) return 5;
    if (strstr(events.last, "frame.very_slow") == NULL) return 6;

    memset(&events, 0, sizeof(events));
    apsis_init(&ctx);
    err[0] = '\0';
    if (apsis_parse_rule_line(&ctx,
                            "temp > 10 warn temp.high cooldown 100ms",
                            1,
                            "contract-test",
                            err,
                            sizeof(err)) != 0) {
        return 7;
    }
    if (apsis_sample_each(&ctx, "temp", 11.0, 10.0,
                        on_contract_event, &events) != 1) return 8;
    if (apsis_sample_each(&ctx, "temp", 12.0, 10.05,
                        on_contract_event, &events) != 0) return 9;
    if (apsis_sample_each(&ctx, "temp", 13.0, 10.101,
                        on_contract_event, &events) != 1) return 10;
    if (events.count != 2) return 11;

    memset(&events, 0, sizeof(events));
    apsis_init(&ctx);
    if (apsis_add_stale_rule(&ctx, "heartbeat", 5.0,
                           APSIS_ERROR, "heartbeat.missing") != 0) return 12;
    if (apsis_sample_each(&ctx, "heartbeat", 1.0, 20.0,
                        on_contract_event, &events) != 0) return 13;
    if (apsis_emit_stale_each(&ctx, 24.9, on_contract_event, &events) != 0) {
        return 14;
    }
    if (apsis_emit_stale_each(&ctx, 25.1, on_contract_event, &events) != 1) {
        return 15;
    }
    if (events.count != 1) return 16;
    if (strstr(events.last, "heartbeat.missing") == NULL) return 17;

    return 0;
}
C

cat > "$tmp/test_dwell.c" <<'C'
#include "apsis.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct test_events {
    unsigned long count;
    char last[APSIS_LINE_MAX];
} test_events;

static void on_event(const char *line, void *user) {
    test_events *events = (test_events *)user;
    events->count++;
    snprintf(events->last, sizeof(events->last), "%s", line ? line : "");
}

int main(void) {
    apsis_dwell_ctx ctx;
    test_events events;
    volatile uint32_t queue_depth = 1201;
    volatile double frame_ms = 12.5;
    int rc;

    memset(&events, 0, sizeof(events));
    apsis_dwell_init(&ctx);
    apsis_dwell_set_event_callback(&ctx, on_event, &events);

    if (apsis_dwell_add_rule(&ctx,
                           "worker.queue.depth",
                           APSIS_GT,
                           1000.0,
                           APSIS_ERROR,
                           "queue.backpressure") != 0) {
        return 1;
    }
    if (apsis_dwell_add_rule(&ctx,
                           "renderer.frame.ms",
                           APSIS_GT,
                           16.6,
                           APSIS_WARN,
                           "frame.slow") != 0) {
        return 2;
    }

    if (apsis_dwell_watch_u32(&ctx,
                            "worker.queue.depth",
                            &queue_depth) != 0) {
        return 3;
    }
    if (apsis_dwell_watch_f64(&ctx,
                            "renderer.frame.ms",
                            &frame_ms) != 0) {
        return 4;
    }

    rc = apsis_dwell_tick(&ctx);
    if (rc != 1) return 5;
    if (events.count != 1) return 6;
    if (strstr(events.last, "queue.backpressure") == NULL) return 7;
    if (ctx.contracts.error_count != 1) return 8;
    if (ctx.contracts.warn_count != 0) return 9;

    frame_ms = 20.0;
    rc = apsis_dwell_tick(&ctx);
    if (rc != 2) return 10;
    if (events.count != 3) return 11;
    if (ctx.contracts.error_count != 2) return 12;
    if (ctx.contracts.warn_count != 1) return 13;

    return 0;
}
C

cat > "$tmp/embedded-main.c" <<'C'
#include "apsis.h"
#include "telemetry_ids.h"

#include <stdint.h>
#include <stdio.h>

static void print_event(const char *line, void *user) {
    (void)user;
    puts(line);
}

int main(void) {
    apsis_dwell_ctx ctx;
    volatile uint32_t queue_depth = 1201;
    int emitted;

    apsis_dwell_init(&ctx);
    apsis_dwell_set_event_callback(&ctx, print_event, NULL);

    if (apsis_dwell_add_rule(&ctx,
                           APSIS_TEL_WORKER_QUEUE_DEPTH,
                           APSIS_GT,
                           1000.0,
                           APSIS_ERROR,
                           "queue.backpressure") != 0) {
        return 1;
    }

    if (apsis_dwell_watch_u32(&ctx,
                            APSIS_TEL_WORKER_QUEUE_DEPTH,
                            &queue_depth) != 0) {
        return 1;
    }

    emitted = apsis_dwell_tick(&ctx);
    return emitted == 1 ? 0 : 1;
}
C

"$CC" $CFLAGS -Iinclude "$tmp/test_dwell.c" libapsis.a -o "$tmp/test_dwell"
"$CC" $CFLAGS -Iinclude "$tmp/test_contracts.c" libapsis.a -o "$tmp/test_contracts"
"$tmp/test_contracts"
"$tmp/test_dwell"

"$CC" $CFLAGS -Iinclude -I"$tmp" "$tmp/embedded-main.c" libapsis.a -o "$tmp/apsis_embedded_example"
"$tmp/apsis_embedded_example" > "$tmp/apsis_embedded_example.out"
