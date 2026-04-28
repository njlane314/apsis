# CTC Coding Standard

This is the project-local coding standard for `ctc`. It is informed by
[GUARDRAILS](GUARDRAILS), but it is
not a NASA compliance claim.

## Applicability

Default profile: `non_nasa_general_software`.

Use a stricter profile only when a maintainer explicitly marks a change as
mission-critical, safety-critical, security-sensitive, embedded, real-time,
flight, or ground support software.

## C Baseline

- C99 source.
- No required third-party runtime dependencies.
- Fixed-size storage is preferred.
- Heap allocation is avoided in runtime paths. If heap allocation is needed, it
  must be bounded, checked, and recorded in [WAIVERS](WAIVERS).
- Public and boundary-facing functions validate pointer, capacity, enum, and
  range inputs before use.
- Non-void return values are checked, propagated, or explicitly discarded with a
  nearby rationale.
- Prefer simple control flow. Do not introduce `goto`, `setjmp`, `longjmp`, or
  recursion without a documented waiver.
- Loops over internal fixed-size arrays must use named bounds. Loops over input
  streams must include line-size limits, parse checks, and explicit error paths.
- Generated files must identify their generator and should not be edited by
  hand.

## Build And Analysis

Required local check before delivery:

```sh
make check CFLAGS='-std=c99 -Wall -Wextra -Wpedantic -Werror -O2'
```

Optional guardrail scan when `rg` is available:

```sh
make guardrail-scan
```

Static-analysis findings, compiler warnings, and guardrail deviations must be
resolved or recorded in [NONCONFORMANCES](NONCONFORMANCES) and
[WAIVERS](WAIVERS).

## Naming And Formatting

- Public library identifiers use the `ctc_` prefix.
- Public dwell identifiers use the `ctc_dwell_` prefix.
- Tool-local static helpers use the tool name as a prefix where practical.
- Keep comments sparse and concrete. Prefer comments that explain invariants,
  bounds, and non-obvious error handling.

## Security And Process Boundaries

- Treat input files, stdin samples, process IDs, paths, symbols, and remote
  process memory as untrusted.
- Do not silently truncate externally supplied names or paths in a way that
  changes meaning.
- Diagnostics go to stderr. Machine-consumable tool output goes to stdout.
- Do not claim NASA compliance, flight readiness, safety certification, or NASA
  security compliance without the evidence listed in the guardrails.
