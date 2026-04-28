# Telemetry Dictionary

Generated from an atlas dictionary.

## Telemetry

| Key | Type | Unit | Description |
|---|---:|---:|---|
| renderer.frame.ms | f64 | ms | Frame render time |
| worker.queue.depth | u32 | count | Pending work items |
| worker.heartbeat.age | f64 | s | Seconds since worker heartbeat |

## Limits

| Key | Op | Value | Level | Event | Cooldown |
|---|---:|---:|---:|---|---:|
| renderer.frame.ms | > | 16.6 | warn | frame.slow | 5s |
| renderer.frame.ms | > | 33.3 | error | frame.very_slow | 5s |
| worker.queue.depth | > | 1000 | error | queue.backpressure | 10s |
| worker.heartbeat.age | stale | 5s | error | worker.heartbeat.missing | 30s |

## Commands

| Command | Description |
|---|---|
| renderer.set_quality | Change render quality |
| worker.dump_state | Write worker state to a diagnostics file |

## Command Arguments

| Command | Argument | Type | Description |
|---|---|---:|---|
| renderer.set_quality | level | enum | Quality preset: low, medium, high |
| worker.dump_state | path | string | Output path |
