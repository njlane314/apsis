#!/usr/bin/env sh
set -eu

awk -F, '
    NR == 1 { next }
    {
        print "compression.mb_per_sec=" $2
        print "decompression.mb_per_sec=" $3
        print "compression.ratio=" $4
        print "binary.bytes=" $5
    }
' "${1:-zstd-bench.csv}"
