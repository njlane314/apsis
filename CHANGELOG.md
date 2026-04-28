# Changelog

## Unreleased

- Imported `trip` and `dwell` with history under `tools/`.
- Added `atlas` as the dictionary compiler.
- Added `libapsis` contract parsing and sampling shared by `trip` and `dwell`.
- Reframed `dwell` around in-process C state watching.
- Added `probe` as a Linux external process variable sampler that emits
  `key=value` telemetry for `trip`.
- Added `bind` as a source-to-symbol/address binding manifest compiler for
  `probe` arguments, JSON, and GitHub Markdown summaries.
- Added `gate` as a one-command `atlas -> bind -> probe -> trip` workflow
  runner that verifies generated watch bindings before sampling.
- Added `bound` to learn reviewable candidate contract limits from known-good
  telemetry samples.
- Added `make uninstall` for staged or prefix-based package cleanup.
- Added an optional `apsis` wrapper script that dispatches to the standalone
  Unix tools without replacing them.
- Added `doctor` and `init --profile probe` to the wrapper.
- Removed scenario-specific example and demo-script directories.
- Extended `probe` with direct ELF symbol lookup, basic C++ demangled-symbol
  matching, `--format`, and direct `--rules` evaluation.
- Added NASA-derived coding guardrails and root agent instructions.
- Added the project coding standard, waiver/nonconformance logs, and removed
  dynamic allocation from `probe` ELF symbol lookup.
- Flattened tool sources to `tools/trip.c`, `tools/dwell.c`, `tools/atlas.c`,
  and `tools/probe.c`.
- Moved core shared C sources under `tools/`.
- Made `check.sh` self-contained and removed the `examples/` directory.
- Moved documentation guardrail files into the root as `GUARDRAILS`, `WAIVERS`,
  and `NONCONFORMANCES`.
- Renamed the project coding standard to `STANDARD.md`.
- Added `apsis_sample_each()` event callbacks so one sample can emit every
  matching rule event through `trip`, `dwell`, and `probe --rules`.
- Reworked stale rules to use last-update time and changed cooldown enforcement
  to monotonic subsecond timing.
- Made the shared rule parser reentrant by removing `strtok()` usage.
- Added probe `--emit`, `--wait-child`, `--leave-running`, launch PATH
  resolution, and JSON non-finite value handling.
- Added `probe symbols` for ELF data-symbol discovery with filtering,
  demangling, and size-based probe type hints.
- Added `probe plan` to normalize `KEY=TYPE@symbol:NAME` and
  `KEY=TYPE@addr:ADDR` watches into runnable `probe` commands.
- Added uniform command aliases: `atlas emit rules|header|doc`,
  `trip check --rules`, and `probe run|attach --symbol KEY=TYPE:SYMBOL`.
- Added `trip --github-summary` for GitHub Actions step-summary output.
- Made `atlas header` reject colliding generated C macro identifiers.
