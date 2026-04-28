#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work="${TMPDIR:-/tmp}/lim-github-actions-example-$$"

cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT INT HUP TERM

mkdir -p "$work"
cp -R "$root/examples/github-actions/project/." "$work/"
cp "$root/lim.c" "$root/lim.h" "$work/"

cd "$work"

cc -std=c99 -O2 -Wall -Wextra lim.c -o lim

cmake -S . -B build
cmake --build build -j2

./build/my_benchmark > telemetry.tlm
printf "binary.bytes=%s\n" "$(wc -c < build/my_program)" >> telemetry.tlm

set +e
./lim -r .lim/rules.lim --summary < telemetry.tlm > lim-events.jsonl
status=$?
set -e

cat lim-events.jsonl

test "$status" -eq 0
grep -q '"id":"frame.slow"' lim-events.jsonl
grep -q '^binary.bytes=' telemetry.tlm
