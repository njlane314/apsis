# apsis

Command, Telemetry, and Contracts for C and Unix pipelines.

`apsis` is a small C library plus boring Unix tools for defining telemetry,
checking runtime contracts, and failing CI when numeric system limits are
violated.

## Probe To Trip Demo

The shortest useful flow is still a Unix pipeline:

```sh
make demo-probe
```

The demo builds a tiny Linux process with global telemetry variables, samples
those variables with `probe`, and sends the samples into `trip`:

```sh
probe run \
  --watch imu.temperature_c=f32@symbol:imu_temperature_c \
  --watch battery.voltage=f32@symbol:battery_voltage \
  --emit samples \
  -n 1 \
  -- ./drone_sim \
  | trip check --rules rules.trip --fail-on never --summary
```

Use `probe symbols ./drone_sim --filter temperature --types` to discover
watchable globals before writing a watch. Use `probe plan` to convert
`name=type@symbol:...` watches into the lower-level probe command form.
If the watches are described in Atlas, `bind` can emit them directly:

```sh
bind emit watch ./drone_sim flight.atlas --verify-types
```

## Tools

- `apsis` is a thin wrapper that dispatches to the standalone tools.
- `atlas` compiles a source-of-truth dictionary into `trip` rules, Markdown
  docs, and C constants.
- `bind` compiles source-to-symbol/address binding manifests into `probe`
  watch arguments, JSON, or GitHub-flavored Markdown summaries.
- `trip` checks external key/value telemetry streams against rules.
- `dwell` watches internal C variables/readers and samples them through
  `libapsis`.
- `probe` samples typed variables from another Linux process and emits
  `key=value` telemetry for `trip`.

The tool boundary is intentional:

```sh
atlas check telemetry.atlas
atlas emit rules telemetry.atlas > rules.trip
atlas emit header telemetry.atlas > telemetry_ids.h
atlas emit doc telemetry.atlas > TELEMETRY.md
bind emit watch ./program telemetry.atlas --verify-types
bind probe telemetry.bind > probe.args
program | trip check --rules rules.trip
probe run --symbol worker.queue.depth=u32:queue_depth -n 10 -- ./program | trip check --rules rules.trip
probe run --symbol worker.queue.depth=u32:queue_depth --rules rules.trip --emit both -- ./program
probe attach --pid 1234 --symbol worker.queue.depth=u32:queue_depth
probe symbols ./program --filter queue --types
probe plan \
  --watch imu.temperature_c=f32@symbol:imu_temperature_c \
  --watch battery.voltage=f32@symbol:battery_voltage \
  -- ./drone_sim
cc -Iinclude app.c libapsis.a
```

The wrapper is optional and keeps the underlying Unix tools intact:

```sh
apsis atlas check telemetry.atlas
apsis emit rules telemetry.atlas > rules.trip
apsis trip check --rules rules.trip < samples.tlm
apsis run --symbol worker.queue.depth=u32:queue_depth -- ./program
apsis symbols ./program --filter queue
apsis doctor
apsis init --profile probe
```

`trip` can also append a GitHub step summary:

```sh
trip check --rules rules.trip --github-summary "$GITHUB_STEP_SUMMARY" \
  < samples.tlm
```

## Build

```sh
make
make check
make demo-probe
```

The code is C99, uses fixed-size storage, and has no runtime dependencies
outside the C/POSIX toolchain.

## Agent Guardrails

Automated coding agents must read [AGENTS.md](AGENTS.md) and
[STANDARD.md](STANDARD.md) before changing code. The detailed
NASA-derived guardrails live in
[GUARDRAILS](GUARDRAILS). These
guardrails are development guidance for this repository, not a NASA compliance
claim.

## Library

The shared contract engine lives in `libapsis`.

```c
#include <apsis.h>

static int print_event(const apsis_event *ev, void *user) {
    char line[APSIS_LINE_MAX];

    (void)user;
    if (apsis_format_event_record(ev, line, sizeof(line)) != 0) return -1;
    puts(line);
    return 0;
}

apsis_ctx ctx;

apsis_init(&ctx);
apsis_add_rule(&ctx, "worker.queue.depth", APSIS_GT, 1000.0,
             APSIS_ERROR, "queue.backpressure");

apsis_sample_each(&ctx, "worker.queue.depth", 1201.0,
                apsis_now_seconds(), print_event, NULL);
```

`apsis_sample_each()` calls the event callback for every matching rule. The older
`apsis_sample()` helper is still available when a caller only needs the first
formatted event line.

Stale rules mean that a key has not been updated within the configured duration.
Use `apsis_emit_stale_each()` from a sampling loop or at stream end to evaluate
those rules. Cooldowns use monotonic seconds where the platform provides them.

Rule evaluation currently uses `double` values. That is suitable for ordinary
telemetry thresholds, but exact equality or ordering on large `i64`/`u64`
counters above `2^53` is not guaranteed in this version.

`dwell` uses the same rule engine for in-process sampling:

```c
apsis_dwell_ctx ctx;
volatile uint32_t queue_depth = 1201;

apsis_dwell_init(&ctx);
apsis_dwell_add_rule(&ctx, APSIS_TEL_WORKER_QUEUE_DEPTH, APSIS_GT,
                   1000.0, APSIS_ERROR, "queue.backpressure");
apsis_dwell_watch_u32(&ctx, APSIS_TEL_WORKER_QUEUE_DEPTH, &queue_depth);
apsis_dwell_tick(&ctx);
```

Generate `APSIS_TEL_*` constants with:

```sh
atlas emit header telemetry.atlas > telemetry_ids.h
```

`atlas emit header` fails if telemetry or command names normalize to colliding C
macro identifiers.

## Probe Notes

`bind` keeps telemetry keys separate from process symbols or fixed addresses:

```text
source motor.temperature f32 symbol motor_state.temperature_c
source imu.temperature_c f32 addr 0x7ffd1234
source cpp.temperature f32 symbol drone::cpp_temperature_c object ./libdrone.so
```

Useful outputs:

```sh
bind emit watch ./program telemetry.atlas --verify-types
bind check telemetry.bind
bind probe telemetry.bind --object ./program
bind json telemetry.bind
bind github telemetry.bind
```

`bind emit watch` reads Atlas telemetry entries, maps keys like
`imu.temperature_c` to symbols like `imu_temperature_c`, and emits
`--watch KEY=TYPE@symbol:SYMBOL@object:OBJECT` arguments for `probe`.
With `--verify-types`, it checks Linux ELF symbol metadata and rejects missing
symbols or size mismatches where symbol sizes are available.

`bind probe` requires explicit probe types. DWARF source-type validation and
struct-field dereference remain reserved capabilities.

Use `probe symbols OBJECT` to discover watchable ELF data symbols before writing
`-s name:type:symbol` watches:

```sh
probe symbols ./program
probe symbols ./program --filter temp
probe symbols ./program --demangle
probe symbols ./program --types
```

`--types` reports ELF symbol kind, binding, size, address, and a size-based
probe type hint. It does not report source-level C types.

Use `probe plan` to turn the newer binding-oriented watch spelling into the
runtime probe invocation:

```sh
probe plan \
  --watch imu.temperature_c=f32@symbol:imu_temperature_c \
  --watch battery.voltage=f32@symbol:battery_voltage \
  -- ./drone_sim
```

The supported watch forms are `KEY=TYPE@symbol:SYMBOL` and
`KEY=TYPE@addr:ADDRESS`.

For direct sampling, the same normalized watch shape is accepted by `run` and
`attach`:

```sh
probe run --symbol imu.temperature_c=f32:imu_temperature_c -- ./drone_sim
probe attach --pid 1234 --symbol imu.temperature_c=f32:imu_temperature_c
```

See [Probe A Global Variable](docs/recipes/probe-a-global-variable.md) for a
complete `probe | trip` recipe.

`probe` does not bypass operating-system access controls. It is intended for
processes you own in lab, debug, simulation, and CI settings. Linux ptrace
scope, same-UID rules, container policy, and process capabilities can all block
remote memory reads.

When `probe` launches a target, it terminates the child after sampling by
default. Use `--wait-child` to wait for natural exit or `--leave-running` to
detach after sampling. With `--rules`, output defaults to events; choose
`--emit samples`, `--emit events`, or `--emit both` explicitly when debugging.

DWARF struct field paths and eBPF/uprobe argument capture are planned features,
not active behavior yet.
