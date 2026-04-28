# Probe A Global Variable

This recipe samples a global variable from a Linux process and checks it with
`trip`.

## 1. Build A Target

```c
#define _POSIX_C_SOURCE 200809L
#include <time.h>

volatile float imu_temperature_c = 82.5f;

int main(void) {
    struct timespec req;

    req.tv_sec = 30;
    req.tv_nsec = 0;
    nanosleep(&req, NULL);
    return 0;
}
```

Compile with symbols:

```sh
cc -std=c99 -O0 -g drone_sim.c -o drone_sim
```

## 2. Discover Symbols

```sh
probe symbols ./drone_sim --filter temperature --types
```

Expected output includes `imu_temperature_c`.

## 3. Write Rules

```text
imu.temperature_c > 80 warn imu.overtemp cooldown 5s
```

Save that as `rules.trip`.

## 4. Emit Watches From Atlas

If the same telemetry is in Atlas:

```text
telemetry imu.temperature_c f32 C "IMU temperature"
```

`bind` can derive the probe watch arguments and check the ELF symbol size:

```sh
bind emit watch ./drone_sim flight.atlas --verify-types
```

## 5. Plan The Probe

```sh
probe plan \
  --watch imu.temperature_c=f32@symbol:imu_temperature_c \
  -- ./drone_sim
```

The plan output is a runnable `probe` command using the lower-level Unix tool
syntax.

## 6. Run Probe Into Trip

```sh
probe run \
  --watch imu.temperature_c=f32@symbol:imu_temperature_c \
  --emit samples \
  -n 1 \
  -- ./drone_sim \
  | trip check --rules rules.trip --fail-on never --summary
```

`probe` emits `key=value` samples. `trip` reads those samples and emits contract
events.

## 7. Wrapper Form

The optional wrapper keeps the same flow:

```sh
apsis run \
  --watch imu.temperature_c=f32@symbol:imu_temperature_c \
  --emit samples \
  -n 1 \
  -- ./drone_sim \
  | apsis trip check --rules rules.trip --fail-on never --summary
```
