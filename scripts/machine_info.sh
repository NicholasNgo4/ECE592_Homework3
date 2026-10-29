#!/usr/bin/env bash
# Record everything needed to identify a machine and its access limits.
# Usage: scripts/machine_info.sh <outfile>
out=${1:-/dev/stdout}
{
  echo "# hostname";            hostname
  echo "# date";                date -Iseconds
  echo "# kernel";              uname -a
  echo "# lscpu";               lscpu
  echo "# microcode";           grep -m1 microcode /proc/cpuinfo
  echo "# cpu family/model/stepping"
  grep -m3 -E '^(cpu family|model|stepping)\s' /proc/cpuinfo
  echo "# perf_event_paranoid"; cat /proc/sys/kernel/perf_event_paranoid
  echo "# transparent hugepages"; cat /sys/kernel/mm/transparent_hugepage/enabled
  echo "# hugepage pools"
  for d in /sys/kernel/mm/hugepages/*/; do
    echo "$(basename "$d"): total=$(cat "$d/nr_hugepages") free=$(cat "$d/free_hugepages")"
  done
  echo "# SGX"
  echo "cpuinfo flags: $(grep -o -w -E 'sgx|sgx_lc' /proc/cpuinfo | sort -u | tr '\n' ' ')"
  ls -l /dev/sgx* /dev/isgx 2>&1
  echo "# SMT active";          cat /sys/devices/system/cpu/smt/active 2>/dev/null || echo unknown
  echo "# CPU frequency governor"
  cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown
  echo "# load at collection time"; uptime
} > "$out" 2>&1
