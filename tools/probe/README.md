# probe

`probe` is a small Unix-style tool for sampling internal variables from a running Linux process.

It is intended for local debugging, simulation, CI, and lab diagnostics on processes you own. It is read-only: it reads typed values from a process and prints `key=value` samples that can be piped into `trip`.

```sh
probe -p 1234 -w imu.temperature_c:f32:0x7ffd01234000
```

or, using a symbol from an executable:

```sh
probe -p 1234 -s imu.temperature_c:f32:imu_temperature_c
```

For C++ globals and static data members, `probe` can match common Itanium ABI
nested names by their demangled form:

```sh
probe -p 1234 -s motor.temp:f32:drone::Motor::temperature_c
```

or, let `probe` launch the target so Linux ptrace permissions are usually straightforward:

```sh
probe -s imu.temperature_c:f32:imu_temperature_c -n 10 -i 100 -- ./drone_sim
```

## Why

The idea is inspired by flight-software memory-dwell patterns: sometimes an internal variable matters operationally, but the program does not normally print it, log it, or expose it as telemetry.

`probe` is the non-cooperative counterpart to an embedded `dwell` library:

- `dwell`: the program registers variables itself.
- `probe`: an external tool samples variables by address or symbol.
- `trip`: evaluates rules over values once they are available.

## Build

```sh
make
```

## Demo

```sh
sh examples/probe/demo.sh
```

Example output:

```text
imu.temperature_c=72.5
battery.voltage=12.38
control.loop_ms=2.14
net.dropped_packets=1
```

Pipe to `trip`:

```sh
probe -s imu.temperature_c:f32:imu_temperature_c -n 20 -i 100 -- ./target \
  | trip -r examples/probe/rules.trip
```

Or evaluate rules directly:

```sh
probe -s imu.temperature_c:f32:imu_temperature_c \
  --rules examples/probe/rules.trip \
  -- ./target
```

## Usage

```text
probe -p PID -w name:type:0xADDR [-i MS] [-n COUNT]
probe -p PID -s name:type:symbol [-o OBJECT] [-i MS] [-n COUNT]
probe -p PID -s root.field.path:type [-o OBJECT] [-i MS] [-n COUNT]
probe -s name:type:symbol [-o OBJECT] [-i MS] [-n COUNT] -- ./program [args...]
```

For a per-watch shared object, append `@OBJECT` to the symbol:

```sh
probe -p 1234 -s shared.temp:f32:shared_temperature@/path/libflight.so
```

Types:

```text
i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 bool
```

Options:

```text
-p, --pid PID              sample an existing process
-w, --watch-addr SPEC      watch name:type:address
-s, --watch-symbol SPEC    watch name:type:symbol
-o, --object PATH          executable/shared object containing symbols
-i, --interval-ms MS       interval between samples, default 1000
-n, --count COUNT          number of samples, default 1
--json                     emit JSONL instead of key=value
--format FORMAT            keyvalue, jsonl, or limlog
--rules PATH               evaluate samples with libctc rules
--fail-on LEVEL            error, warn, info, or never
--delay-ms MS              startup delay before sampling in run mode
-v, --verbose              show resolved addresses
--ebpf, --uprobe           reserved for future eBPF/uprobe mode
```

## Compilation notes

Symbol mode works best when:

- the variable is global or file-static;
- the binary is not stripped;
- the program is built with useful symbols, for example `-g`;
- the variable is not optimized away;
- the process is one you own and Linux permissions allow memory inspection.

For local variables, optimized variables, registers, and inlined code, use `dwell` instead. A source-level local variable may not have a stable address at runtime.

## How it reads memory

`probe` uses Linux `process_vm_readv(2)` to read the target process. Symbol mode reads ELF `.symtab` and `.dynsym` directly, finds the runtime load base using `/proc/<pid>/maps`, and reads the computed address. It does not shell out to `nm`.

## Limitations

This v0.1 is intentionally narrow:

- Linux only.
- Read-only.
- Symbol mode is for global/static symbols, not arbitrary locals.
- It depends on ordinary OS permissions; it does not bypass ptrace restrictions.
- DWARF field-path syntax is accepted but not implemented yet:
  `probe -p PID -s drone_state.imu.temperature_c:f32` fails fast with a clear diagnostic.
- It does not support structs/field paths yet.
- eBPF/uprobe mode is reserved but not implemented yet.

## Relationship to the suite

A clean CTC suite boundary is:

```text
probe   external variable sampler for running processes
dwell   cooperative internal variable contracts
trip    rule evaluator for key=value telemetry
```

The intended flow is:

```sh
  probe ... | trip -r rules.trip
```

or:

```sh
dwell-enabled-program | trip -r rules.trip
```
