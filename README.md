# ctc

Command, Telemetry, and Contracts for C and Unix pipelines.

`ctc` is a small C library plus boring Unix tools for defining telemetry,
checking runtime contracts, and failing CI when numeric system limits are
violated.

## Tools

- `atlas` compiles a source-of-truth dictionary into `trip` rules, Markdown
  docs, and C constants.
- `trip` checks external key/value telemetry streams against rules.
- `dwell` watches internal C variables/readers and samples them through
  `libctc`.
- `probe` samples typed variables from another Linux process and emits
  `key=value` telemetry for `trip`.

The tool boundary is intentional:

```sh
atlas rules examples/rover/telemetry.atlas > rules.trip
program | trip -r rules.trip
probe -s worker.queue.depth:u32:queue_depth -n 10 -- ./program | trip -r rules.trip
probe -s worker.queue.depth:u32:queue_depth --rules rules.trip -- ./program
cc -Iinclude -Iexamples/rover examples/embedded/main.c libctc.a
```

## Build

```sh
make
make check
```

The code is C99, uses fixed-size storage, and has no runtime dependencies
outside the C/POSIX toolchain.

## Agent Guardrails

Automated coding agents must read [AGENTS.md](AGENTS.md) and
[docs/nasa_coding_guardrails.yml](docs/nasa_coding_guardrails.yml) before
changing code. These guardrails are NASA-derived development guidance for this
repository, not a NASA compliance claim.

## Library

The shared contract engine lives in `libctc`.

```c
#include <ctc.h>

ctc_ctx ctx;
char event[CTC_LINE_MAX];

ctc_init(&ctx);
ctc_add_rule(&ctx, "worker.queue.depth", CTC_GT, 1000.0,
             CTC_ERROR, "queue.backpressure");

if (ctc_sample(&ctx, "worker.queue.depth", 1201.0,
               event, sizeof(event)) > 0) {
    puts(event);
}
```

`dwell` uses the same rule engine for in-process sampling:

```c
ctc_dwell_ctx ctx;
volatile uint32_t queue_depth = 1201;

ctc_dwell_init(&ctx);
ctc_dwell_add_rule(&ctx, CTC_TEL_WORKER_QUEUE_DEPTH, CTC_GT,
                   1000.0, CTC_ERROR, "queue.backpressure");
ctc_dwell_watch_u32(&ctx, CTC_TEL_WORKER_QUEUE_DEPTH, &queue_depth);
ctc_dwell_tick(&ctx);
```

Generate `CTC_TEL_*` constants with:

```sh
atlas header examples/rover/telemetry.atlas > telemetry_ids.h
```
