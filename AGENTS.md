# Agent Instructions

This repository uses a compact, agent-facing C and Unix-tool development
profile. It is inspired by high-reliability software guidance, but it is not a
NASA compliance claim.

Default profile: `non_nasa_general_software`.

Use stricter handling only when a maintainer or project document explicitly
marks work as mission-critical, safety-critical, security-sensitive, embedded,
real-time, flight, or ground support software.

## Core Rules

- Prefer simple, statically analyzable C99.
- Keep runtime storage fixed-size where practical.
- Avoid heap allocation in runtime paths unless the need is bounded, checked,
  and explained in the change record.
- Prefer simple control flow. Do not introduce `goto`, `setjmp`, `longjmp`, or
  recursion without a clear rationale.
- Keep loops bounded by inspection where practical. Loops over input streams
  must have line-size limits, parse checks, and explicit error paths.
- Public and boundary-facing functions must validate pointer, capacity, enum,
  and range inputs before use.
- Non-void return values must be checked, propagated, or explicitly discarded
  with a nearby rationale.
- Minimize mutable global state and keep data at the narrowest useful scope.
- Generated files must identify their generator and should not be edited by
  hand.

## Naming And Formatting

- Public library identifiers use the `apsis_` prefix.
- Public dwell identifiers use the `apsis_dwell_` prefix.
- Tool-local static helpers use the tool name as a prefix where practical.
- Keep comments sparse and concrete. Prefer comments that explain invariants,
  bounds, and non-obvious error handling.

## Security And Process Boundaries

- Treat input files, stdin samples, process IDs, paths, symbols, and remote
  process memory as untrusted.
- Do not silently truncate externally supplied names or paths in a way that
  changes meaning.
- Diagnostics go to stderr. Machine-consumable tool output goes to stdout.
- Do not suppress compiler or static-analysis findings without a written
  rationale in the relevant commit, PR, or issue.

## Build And Delivery

Build with the repository Makefile before delivery when the toolchain supports
it:

```sh
make CFLAGS='-std=c99 -Wall -Wextra -Wpedantic -Werror -O2'
```

Report any build commands that could not be run, plus any remaining warnings or
findings.

Never state that this repository, code, or generated output is NASA compliant,
flight software ready, safety certified, or secure by NASA standards.
