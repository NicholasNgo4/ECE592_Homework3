#!/usr/bin/env bash
# Enclave comparison: run the ROB/PRF and 4 KiB TLB experiments inside an
# SGX enclave (hw3_sgx) and as a normal process (hw3_native) on the same
# machine and CPU, alternating, for `rounds` rounds. Raw data goes to
# data/<host>/enclave/. Needs /dev/sgx_enclave and the Intel SGX SDK.
#
# Usage: enclave/run_enclave.sh [cpu] [rounds]     (defaults: 2, 2)
set -euo pipefail
cd "$(dirname "$0")/.."

cpu=${1:-2}
rounds=${2:-2}
host=$(hostname -s)
stamp=$(date +%Y%m%d-%H%M%S)
dir=data/$host/enclave
log=$dir/run_$stamp.log
mkdir -p "$dir"

make -s
make -s -C enclave
scripts/machine_info.sh "data/$host/machine_info.txt"
echo "enclave comparison on $host, cpu $cpu, $rounds rounds, $stamp" | tee "$log"
uptime | tee -a "$log"
(cd enclave && ./hw3_native -t probe -c "$cpu" && ./hw3_sgx -t probe -c "$cpu") 2>&1 | tee -a "$log"

out=$PWD/$dir
for r in $(seq 1 "$rounds"); do
  # Alternate which side goes first so slow drift on a shared machine
  # does not favor either one.
  if [ $((r % 2)) -eq 1 ]; then order="native sgx"; else order="sgx native"; fi
  for t in rob tlb; do
    for m in $order; do
      echo "== round $r: $t $m" | tee -a "$log"
      (cd enclave && ./hw3_$m -t "$t" -c "$cpu" -o "$out/${t}_${m}_${stamp}_r$r.csv") 2>&1 | tee -a "$log"
    done
  done
done
# Linear refinement around the L1 DTLB and L2 TLB edges, both sides.
for range in 80:112:1 1024:3072:24; do
  for m in native sgx; do
    echo "== refine $range: tlb $m" | tee -a "$log"
    (cd enclave && ./hw3_$m -t tlb -c "$cpu" -L "$range" \
      -o "$out/refine_tlb_${m}_${stamp}_${range%%:*}.csv") 2>&1 | tee -a "$log"
  done
done
uptime | tee -a "$log"
echo "Done. Raw data in $dir/. Plot with: python3 analysis/plot_enclave.py" | tee -a "$log"
