# lim

`lim` is a tiny command-line telemetry limit checker.

Pipe `key=value` telemetry into `lim`, define threshold rules, and get JSONL warning/error events on stdout. It is designed for ordinary native programs and small CI-friendly telemetry checks.

## Example

```sh
cc -std=c99 -Wall -Wextra -O2 lim.c -o lim
./lim -r examples/rules.lim --summary < examples/sample.tlm
```

Input:

```text
temperature=82.4
queue.depth=1402
frame.ms=18.7
battery.voltage=11.9
```

Rules:

```text
temperature      >   80         warn   temperature.high
queue.depth      >   1000       error  queue.backpressure
frame.ms         >   16.6       warn   frame.slow
battery.voltage  <   10.5       error  battery.low
```

Output:

```json
{"ts":1760000000,"level":"warn","id":"temperature.high","key":"temperature","op":">","threshold":80,"value":82.400000000000006}
{"ts":1760000000,"level":"error","id":"queue.backpressure","key":"queue.depth","op":">","threshold":1000,"value":1402}
{"ts":1760000000,"level":"warn","id":"frame.slow","key":"frame.ms","op":">","threshold":16.600000000000001,"value":18.699999999999999}
```

`lim` exits with status `1` if an `error` rule trips. Use `--fail-on warn` to fail on warnings too, or `--fail-on never` to always return success unless input or rules are invalid.

## Build

```sh
make
make test
```

Or directly:

```sh
cc -std=c99 -Wall -Wextra -O2 lim.c -o lim
```

## GitHub Actions

Copy-paste deployment examples are available in `examples/github-actions/`.

The common pattern is:

```sh
./my_test_or_benchmark | ./lim -r .groundline/rules.lim
```

The example workflow builds `lim`, checks generated telemetry against `.groundline/rules.lim`, writes JSONL events to the GitHub Actions step summary, uploads telemetry artifacts, and fails the workflow when an `error` rule trips.

## Rule format

```text
<key> <op> <number> <level> <event_id>
```

Operators:

```text
> >= < <= == !=
```

Levels:

```text
info warn error
```

Comments start with `#`.

## Input format

`lim` accepts either:

```text
key=value
```

or:

```text
key value
```

Keys may contain letters, digits, `_`, `-`, `.`, `:`, and `/`.

## Embedding

You can also compile `lim.c` into a C or C++ program. Define `LIM_NO_MAIN` to omit the CLI entry point.

```c
#include "lim.h"
#include <stdio.h>

int main(void) {
    lim_ctx ctx;
    char event[512];

    lim_init(&ctx);
    lim_add_rule(&ctx, "temperature", LIM_OP_GT, 80.0, LIM_LEVEL_WARN, "temperature.high");

    if (lim_sample(&ctx, "temperature", 82.4, event, sizeof(event)) > 0) {
        puts(event);
    }
}
```

Build embedded example:

```sh
cc -std=c99 -Wall -Wextra -O2 -DLIM_NO_MAIN example.c lim.c -o example
```

## Design notes

`lim` is deliberately small:

- one `.c` file and one `.h` file
- C99
- no external dependencies
- stdin/stdout/stderr interface
- JSONL output
- nonzero exit status for CI use

The first version is intentionally a command-line filter, not a daemon, dashboard, or cloud service.

## License

MIT.
