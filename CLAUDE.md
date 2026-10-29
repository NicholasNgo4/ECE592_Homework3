# HW3 handoff notes for Claude Code sessions

Lab machines each have their own local `/home`, so Claude conversation
history and memory don't carry over between machines. This file, README.md,
`data/<host>/`, and git history are the only shared state. Read README.md
first: it lists the selected questions (Q4, Q5, Q6, Q8, Q9 = 120 pts), the
method, and the status of each.

## On a new machine
1. `scripts/run_all.sh <idle-cpu>` builds, records `machine_info.txt`, and runs every sweep.
2. `python3 analysis/plot_rob_prf.py` re-plots every host side by side.
3. Update the Machines table and Status column in README.md, and add a row to AI_LOG.md.

## Conventions
- Timed code lives only in generated assembly (`exp/rob_prf/gen_kernels.py`), so the
  compiler can't touch it and the same objects can be linked into an SGX enclave.
- Never JIT. Enclaves can't easily use RWX pages.
- Raw data is never overwritten: each run writes a timestamped CSV.
- Keep measured findings separate from published specs in all write-ups.
- Cache and prefetcher reverse engineering is out of scope for this assignment.
