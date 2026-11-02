#!/usr/bin/env bash
# TLB sweeps that also count DTLB events per access, as independent evidence
# that each timing step is a TLB step. Needs perf access (perf_event_paranoid
# <= 2) and an AMD Zen 2/3 core: the raw encodings below are AMD-specific
# (config = umask << 8 | event; PMC 0x45 = L1 DTLB miss, 0x46 = table walks).
# At most 4 events: the NMI watchdog and the cycle counter take 2 of the 6
# core counters, and rows with pmu_muxed=1 would be unreliable.
#
# Usage: scripts/tlb_pmu.sh [cpu]     (cpu = logical CPU to pin to, default 2)
set -euo pipefail
cd "$(dirname "$0")/.."

cpu=${1:-2}
host=$(hostname -s)
stamp=$(date +%Y%m%d-%H%M%S)
dir=data/$host
log=$dir/run_$stamp.log
mkdir -p "$dir/tlb"

if ! grep -q AuthenticAMD /proc/cpuinfo; then
  echo "tlb_pmu.sh: event encodings are AMD-only; skipping on $host" >&2
  exit 2
fi

ev_4k=l1miss=0xff45,l2hit=0x0145,l2miss=0x1045,coal_hit=0x0245
ev_2m=l1miss=0xff45,l2hit=0x0445,l2miss=0x4045,walks=0x0346

make -s
echo "tlb_pmu on $host, cpu $cpu, $stamp" | tee "$log"
echo "4k events: $ev_4k" | tee -a "$log"
echo "2m events: $ev_2m" | tee -a "$log"

build/tlb -s 4k -c "$cpu" -e "$ev_4k" -o "$dir/tlb/pmu_4k_$stamp.csv" 2>&1 | tee -a "$log"
build/tlb -s 4k -c "$cpu" -L 48:80:1 -e "$ev_4k" \
  -o "$dir/tlb/pmu_l1_4k_$stamp.csv" 2>&1 | tee -a "$log"
build/tlb -s 4k -c "$cpu" -L 1024:3072:24 -e "$ev_4k" \
  -o "$dir/tlb/pmu_refine_4k_$stamp.csv" 2>&1 | tee -a "$log"
build/tlb -s 2m -c "$cpu" -M 4096 -e "$ev_2m" -o "$dir/tlb/pmu_2m_$stamp.csv" 2>&1 | tee -a "$log"
build/tlb -s 2m -c "$cpu" -M 4096 -L 48:80:1 -e "$ev_2m" \
  -o "$dir/tlb/pmu_l1_2m_$stamp.csv" 2>&1 | tee -a "$log"
build/tlb -s 2m -c "$cpu" -M 4096 -L 1024:3072:24 -e "$ev_2m" \
  -o "$dir/tlb/pmu_refine_2m_$stamp.csv" 2>&1 | tee -a "$log"

muxed=$(cat "$dir"/tlb/pmu_*_"$stamp".csv | awk -F, 'NR > 1 && $NF == 1' | wc -l)
echo "rows with multiplexed counters: $muxed" | tee -a "$log"
