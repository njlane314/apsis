#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work="${TMPDIR:-/tmp}/lim-deployment-examples-$$"

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT INT HUP TERM

mkdir -p "$work"
cc -std=c99 -O2 -Wall -Wextra "$root/lim.c" -o "$work/lim"

run_case() {
    name=$1
    input=$2
    expected_event=$3
    dir="$root/examples/deployments/$name"

    "$dir/to-lim.sh" "$dir/$input" > "$work/$name.tlm"

    set +e
    "$work/lim" -r "$dir/rules.lim" --summary < "$work/$name.tlm" > "$work/$name-events.jsonl"
    status=$?
    set -e

    echo "== $name telemetry =="
    cat "$work/$name.tlm"
    echo "== $name events =="
    cat "$work/$name-events.jsonl"

    test "$status" -eq 0
    grep -q "\"id\":\"$expected_event\"" "$work/$name-events.jsonl"
}

run_case llama llama-bench.csv memory.high
run_case zstd zstd-bench.csv binary.large
run_case px4 px4-sitl.log sensor.timeout
