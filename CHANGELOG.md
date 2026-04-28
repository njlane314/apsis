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
