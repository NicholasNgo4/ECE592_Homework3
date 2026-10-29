#!/usr/bin/env bash
# Build and run every experiment on this machine, saving raw data under
# data/<hostname>/. Safe to re-run: each run gets a timestamped file.
#
# Usage: scripts/run_all.sh [cpu]     (cpu = logical CPU to pin to, default 2)
set -euo pipefail
cd "$(dirname "$0")/.."

cpu=${1:-2}
host=$(hostname -s)
stamp=$(date +%Y%m%d-%H%M%S)
dir=data/$host
mkdir -p "$dir/rob_prf"

make -s
scripts/machine_info.sh "$dir/machine_info.txt"

echo "== ROB / PRF sweep on $host (cpu $cpu)"
build/rob_prf -c "$cpu" -o "$dir/rob_prf/sweep_$stamp.csv"

# TLB experiments (exp/tlb) will be added here.

echo "Done. Raw data in $dir/. Plot with: python3 analysis/plot_rob_prf.py"
