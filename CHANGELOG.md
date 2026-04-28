# Changelog

## Unreleased

- Imported `trip` and `dwell` with history under `tools/`.
- Added `atlas` as the dictionary compiler.
- Added `libctc` contract parsing and sampling shared by `trip` and `dwell`.
- Reframed `dwell` around in-process C state watching.
- Added `probe` as a Linux external process variable sampler that emits
  `key=value` telemetry for `trip`.
- Extended `probe` with direct ELF symbol lookup, basic C++ demangled-symbol
  matching, `--format`, and direct `--rules` evaluation.
- Added NASA-derived coding guardrails and root agent instructions.
- Added the project coding standard, waiver/nonconformance logs, and removed
  dynamic allocation from `probe` ELF symbol lookup.
- Flattened tool sources to `tools/trip.c`, `tools/dwell.c`, `tools/atlas.c`,
  and `tools/probe.c`.
- Moved `make check` coverage into `check.sh` and removed the `tests/`
  directory.
- Made `check.sh` self-contained and removed the `examples/` directory.
- Moved documentation guardrail files into the root as `GUARDRAILS`, `WAIVERS`,
  and `NONCONFORMANCES`.
- Renamed the project coding standard to `STANDARD.md`.
