#!/bin/sh
set -eu

: "${CC:=cc}"
: "${CFLAGS:=-std=c99 -Wall -Wextra -Wpedantic -O2}"

if [ "$(uname -s)" != "Linux" ]; then
    echo "demo-probe: probe runtime sampling is Linux-only; skipping demo"
    exit 0
fi

tmp=${TMPDIR:-/tmp}/apsis-demo-probe.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"

cat > "$tmp/drone_sim.c" <<'C'
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <time.h>

volatile float imu_temperature_c = 82.5f;
volatile float battery_voltage = 12.1f;

int main(void) {
    struct timespec req;

    req.tv_sec = 2;
    req.tv_nsec = 0;
    nanosleep(&req, NULL);
    return 0;
}
C

cat > "$tmp/rules.trip" <<'RULES'
imu.temperature_c > 80 warn imu.overtemp cooldown 1s
battery.voltage < 10.8 error battery.low cooldown 1s
RULES

"$CC" $CFLAGS "$tmp/drone_sim.c" -o "$tmp/drone_sim"

echo "probe | trip demo"
./probe run \
    --watch imu.temperature_c=f32@symbol:imu_temperature_c \
    --watch battery.voltage=f32@symbol:battery_voltage \
    --emit samples \
    -n 1 \
    -- "$tmp/drone_sim" \
    | ./trip check --rules "$tmp/rules.trip" --fail-on never --summary
