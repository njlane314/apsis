#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    echo "test_probe: skipped on non-Linux"
    exit 0
fi

tmp=${TMPDIR:-/tmp}/ctc-probe-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

cc -std=c99 -O0 -g examples/probe/target.c -o "$tmp/probe-target"

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
    --rules examples/probe/rules.trip \
    --fail-on warn \
    -- "$tmp/probe-target" \
    > "$tmp/events.out"
status=$?
set -e

test "$status" -eq 1
grep -q 'warn	net.packet_loss' "$tmp/events.out"

set +e
./probe -p $$ -s drone_state.imu.temperature_c:f32 > "$tmp/dwarf.out" 2> "$tmp/dwarf.err"
dwarf_status=$?
set -e
test "$dwarf_status" -eq 2
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
