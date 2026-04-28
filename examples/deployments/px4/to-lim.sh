#!/usr/bin/env sh
set -eu

awk '
    $1 == "PX4_SIM" && $2 == "metric" {
        print $3 "=" $4
    }
' "${1:-px4-sitl.log}"
