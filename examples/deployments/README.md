# Runnable deployment examples

These examples test `lim` against lightweight fixtures that mimic common CI
gates:

- `llama/` converts llama-bench-style CSV into throughput and memory metrics.
- `zstd/` converts compression benchmark CSV into speed, ratio, and size
  metrics.
- `px4/` converts PX4-style simulation health output into timing and health
  metrics.

Run all examples:

```sh
examples/deployments/test-all.sh
```

Each example emits telemetry, runs `lim`, and expects only warning-level events
so the deployment gate succeeds while still proving that events are produced.
