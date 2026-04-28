# GitHub Actions deployment

`lim` works well as a small CI/runtime gate. A project emits numeric
telemetry, `lim` checks those values against rules, and GitHub blocks the pull
request when an `error` rule trips.

Core pattern:

```sh
./my_test_or_benchmark | ./lim -r .groundline/rules.lim
```

Example telemetry:

```text
frame.ms=18.7
queue.depth=1402
binary.bytes=4812032
tokens.per_second=72.4
parse.gbps=3.8
compression.ratio=2.91
```

## Copy into a project

The `project/` directory shows the files a user repo would add:

```text
.groundline/
  rules.lim
.github/
  workflows/
    lim.yml
```

`project/.groundline/rules.lim` contains example thresholds for latency,
backpressure, binary size, inference throughput, and parser throughput.

`project/.github/workflows/lim.yml` builds a project, generates telemetry,
runs `lim`, writes the JSONL result to `$GITHUB_STEP_SUMMARY`, and uploads
`telemetry.tlm` plus `lim-events.jsonl` as artifacts.

## Integration levels

Zero-code integration:

```sh
./bench_parser | awk '{print "parse.gbps="$1}' | ./lim -r .groundline/parser.lim
```

Test harness integration:

```c
printf("frame.ms=%f\n", frame_ms);
printf("queue.depth=%d\n", queue_depth);
printf("heap.bytes=%zu\n", heap_bytes);
```

Embedded integration:

```sh
cc -std=c99 -Wall -Wextra -O2 -DLIM_NO_MAIN example.c lim.c -o example
```

For the first release, the zero-code and CI patterns are the easiest adoption
paths: no hosted service, account, dashboard, or source upload is required.

## Good fits

`lim` is a natural fit for projects that already produce metrics during tests,
benchmarks, simulations, or release checks:

- C/C++ benchmarks that report latency, throughput, or memory use
- local AI runtime checks for tokens/sec, prompt throughput, and memory use
- compression libraries checking speed, ratio, and binary size
- parser libraries checking GB/s throughput and parse error counts
- database benchmarks checking query time, memory, and rows/sec
- robotics or autopilot simulations checking timing and health counters
- OpenTelemetry-style metric exports converted into simple `key=value` lines
