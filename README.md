# apsis

Command, Telemetry, and Contracts for C and Unix pipelines.

`apsis` is a small C library plus boring Unix tools for defining telemetry,
checking runtime contracts, and failing CI when numeric system limits are
violated.

## Workflow

The shortest useful flow is still a Unix pipeline. `probe` emits numeric
samples, and `trip` checks those samples against rules:

```sh
probe run \
  --watch metric.alpha=f32@symbol:metric_alpha \
  --watch metric.beta=f32@symbol:metric_beta \
  --emit samples \
  -n 1 \
  -- ./program \
  | trip check --rules rules.trip --fail-on never --summary
```

Use `probe symbols ./program --filter metric --types` to discover watchable
globals before writing a watch. Use `probe plan` to convert
`name=type@symbol:...` watches into the lower-level probe command form.
If the watches are described in Atlas, `bind` can emit them directly:

```sh
bind emit watch ./program system.atlas --verify-types
```

For the full workflow, `gate` runs `atlas`, `bind`, `probe`, and `trip`
together:

```sh
gate \
  --atlas .apsis/system.atlas \
  --binary build/app \
  --count 50 \
  --interval 100ms \
  --fail-on error \
  -- build/app arg1 arg2
```

That command expands to the same tool boundary you would write by hand:

```sh
atlas emit rules "$atlas" > "$tmp/rules.trip"
bind emit watch "$binary" "$atlas" --verify-types > "$tmp/watch.args"
probe run $(cat "$tmp/watch.args") --emit samples -- "$@" \
  | trip check --rules "$tmp/rules.trip" --fail-on "$fail_on"
```

## Tools

- `apsis` is a thin wrapper that dispatches to the standalone tools.
- `atlas` compiles a source-of-truth dictionary into `trip` rules, Markdown
  docs, and C constants.
- `bind` compiles source-to-symbol/address binding manifests into `probe`
  watch arguments, JSON, or GitHub-flavored Markdown summaries.
- `bound` learns reviewable candidate limits from known-good numeric samples.
- `trip` checks external key/value telemetry streams against rules.
- `dwell` watches internal C variables/readers and samples them through
  `libapsis`.
- `gate` runs the normal `atlas -> bind -> probe -> trip` workflow as one
  command while keeping the individual tools available.
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
gate --atlas telemetry.atlas --binary ./program -- ./program
program | trip check --rules rules.trip
bound learn samples.tlm --emit atlas-patch
probe run --symbol metric.alpha=u32:metric_alpha -n 10 -- ./program | trip check --rules rules.trip
probe run --symbol metric.alpha=u32:metric_alpha --rules rules.trip --emit both -- ./program
probe attach --pid 1234 --symbol metric.alpha=u32:metric_alpha
probe symbols ./program --filter metric --types
probe plan \
  --watch metric.alpha=f32@symbol:metric_alpha \
  --watch metric.beta=f32@symbol:metric_beta \
  -- ./program
cc -Isrc app.c libapsis.a
```

The wrapper is optional and keeps the underlying Unix tools intact:

```sh
apsis atlas check telemetry.atlas
apsis emit rules telemetry.atlas > rules.trip
apsis trip check --rules rules.trip < samples.tlm
apsis gate --atlas telemetry.atlas --binary ./program -- ./program
apsis bound learn samples.tlm --emit report
apsis run --symbol metric.alpha=u32:metric_alpha -- ./program
apsis symbols ./program --filter metric
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
make install PREFIX=/usr/local
make uninstall PREFIX=/usr/local
```

The code is C99, uses fixed-size storage, and has no runtime dependencies
outside the C/POSIX toolchain.

## Agent Guardrails

Automated coding agents must follow [AGENTS.md](AGENTS.md) before changing
code. The guidance is project-local development policy, not a NASA compliance
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
apsis_add_rule(&ctx, "metric.alpha", APSIS_GT, 1000.0,
             APSIS_ERROR, "metric.alpha.high");

apsis_sample_each(&ctx, "metric.alpha", 1201.0,
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
volatile uint32_t metric_alpha = 1201;

apsis_dwell_init(&ctx);
apsis_dwell_add_rule(&ctx, APSIS_TEL_METRIC_ALPHA, APSIS_GT,
                   1000.0, APSIS_ERROR, "metric.alpha.high");
apsis_dwell_watch_u32(&ctx, APSIS_TEL_METRIC_ALPHA, &metric_alpha);
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
source metric.alpha f32 symbol metric_alpha
source metric.beta f32 addr 0x7ffd1234
source metric.gamma f32 symbol ns::metric_gamma object ./libmetrics.so
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
`metric.alpha` to symbols like `metric_alpha`, and emits
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
  --watch metric.alpha=f32@symbol:metric_alpha \
  --watch metric.beta=f32@symbol:metric_beta \
  -- ./program
```

The supported watch forms are `KEY=TYPE@symbol:SYMBOL` and
`KEY=TYPE@addr:ADDRESS`.

For direct sampling, the same normalized watch shape is accepted by `run` and
`attach`:

```sh
probe run --symbol metric.alpha=f32:metric_alpha -- ./program
probe attach --pid 1234 --symbol metric.alpha=f32:metric_alpha
```

## Bound Notes

`bound` learns observed ranges from known-good numeric samples and emits
candidate contracts for review:

```sh
bound learn samples.tlm --emit report
bound learn samples.tlm --margin 20% --min-samples 30 --emit atlas-patch
```

Generated limits are marked as learned suggestions. They are not automatic
safety evidence, and `bound` never modifies Atlas files directly.

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
