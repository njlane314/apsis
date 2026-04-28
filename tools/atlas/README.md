# atlas

`atlas` is a small line-oriented dictionary compiler for command/telemetry/contract suites.
It turns one source file into rule files, Markdown documentation, and C header constants.

```sh
cc -std=c99 -Wall -Wextra -Wpedantic -O2 atlas.c -o atlas
./atlas check ../../examples/rover/telemetry.atlas
./atlas rules ../../examples/rover/telemetry.atlas > rules.trip
./atlas doc ../../examples/rover/telemetry.atlas > TELEMETRY.md
./atlas header ../../examples/rover/telemetry.atlas > telemetry_ids.h
```
