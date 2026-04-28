#!/usr/bin/env sh
set -eu

cc -std=c99 -O0 -g examples/probe/target.c -o target
cc -std=c99 -O2 -Wall -Wextra tools/probe/probe.c -o probe

./probe \
  -s imu.temperature_c:f32:imu_temperature_c \
  -s battery.voltage:f32:battery_voltage \
  -s control.loop_ms:f64:control_loop_ms \
  -s net.dropped_packets:u32:dropped_packets \
  -n 5 -i 100 -- ./target
