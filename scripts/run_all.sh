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
log=$dir/run_$stamp.log
mkdir -p "$dir/rob_prf" "$dir/tlb"

make -s
scripts/machine_info.sh "$dir/machine_info.txt"
echo "run_all on $host, cpu $cpu, $stamp" | tee "$log"

echo "== Q4/Q5 ROB / PRF sweep" | tee -a "$log"
build/rob_prf -c "$cpu" -o "$dir/rob_prf/sweep_$stamp.csv" 2>&1 | tee -a "$log"

echo "== Q6/Q8/Q9 TLB coarse sweeps" | tee -a "$log"
for size in 4k 2m 1g; do
  # Exit status 2 = page size unavailable here; recorded in the log as a
  # documented limitation rather than failing the whole run.
  if ! build/tlb -s "$size" -c "$cpu" -o "$dir/tlb/${size}_$stamp.csv" 2>&1 | tee -a "$log"; then
    echo "$size: UNSUPPORTED on $host (see message above)" | tee -a "$log"
    rm -f "$dir/tlb/${size}_$stamp.csv"
  fi
done

echo "== TLB refinement sweeps around each detected step" | tee -a "$log"
k=0
python3 analysis/plot_tlb.py --ranges "$host" | while read -r size range; do
  echo "refine $size $range" | tee -a "$log"
  build/tlb -s "$size" -c "$cpu" -L "$range" -o "$dir/tlb/refine_${size}_${stamp}_$k.csv" 2>&1 | tee -a "$log"
  k=$((k + 1))
done

echo "Done. Raw data in $dir/. Plot with:"
echo "  python3 analysis/plot_rob_prf.py && python3 analysis/plot_tlb.py"
