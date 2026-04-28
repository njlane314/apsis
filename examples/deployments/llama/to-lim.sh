#!/usr/bin/env sh
set -eu

awk -F, '
    NR == 1 { next }
    {
        phase = $1
        gsub(/[^A-Za-z0-9_.:-]/, "_", phase)
        print phase ".tokens_per_second=" $2
        if (!seen_memory) {
            print "memory.mb=" $3
            seen_memory = 1
        }
    }
' "${1:-llama-bench.csv}"
