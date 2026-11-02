# AI-Use Log (Track B)

| Date | Tool | Task / artifact | Help received | What I verified and can explain |
|---|---|---|---|---|
| 2026-10-29 | Claude (Claude Code) | Question selection | Recommended Q4, Q5, Q6, Q8, Q9 (120 pts) and explained the trade-offs | |
| 2026-10-29 | Claude (Claude Code) | ARM scoping | Explained what an AArch64 port would require; decided to keep ARM out of scope | |
| 2026-10-29 | Claude (Claude Code) | `exp/tlb/`, `analysis/plot_tlb.py`, TLB parts of `run_all.sh` | Wrote the TLB kernel and driver (alias/private/dense patterns, verified 2 MiB pages), step and knee detection, and automatic refinement sweeps; found and fixed the L2-set-conflict and partial-THP artifacts; ran on `upgrade` | |
| 2026-10-29 | Claude (Claude Code) | Repo structure, `common/`, `exp/rob_prf/`, `scripts/`, `analysis/plot_rob_prf.py`, READMEs | Wrote the ROB/PRF kernel generator, sweep driver, run/plot scripts, and documentation; ran the first sweep on `upgrade` | |
| 2026-11-02 | Claude (Claude Code) | Machine 2 runs on `skylark` (Zen 2) | Ran `run_all.sh`, a second ROB/PRF sweep, and hand-ranged L2 TLB refinements (4 KiB 1024–3072; 2 MiB extended to 4096 pages); updated README tables | |
| 2026-11-02 | Claude (Claude Code) | `build/tlb -e`, `raw_open`/`raw_read` in `common/`, `scripts/tlb_pmu.sh` | Added raw PMU event counting (with multiplexing detection) around each timed repetition; ran DTLB-miss counter sweeps on `skylark`; used them to confirm L1/L2 TLB capacities, compute page-walk cost, and separate the AMD L1D way-predictor confound from TLB effects | |
| 2026-11-02 | Claude (Claude Code) | Repeatability runs on `skylark` | Repeated the coarse TLB sweeps on CPU 2 and CPU 40 (both NUMA nodes) and the ROB/PRF sweep on CPU 40; compared all runs | |
| 2026-11-02 | Claude (Claude Code) | Enclave feasibility on `skylark`; SEV checks in `scripts/machine_info.sh` | Probed SGX/SEV support (cpuinfo, CPUID 0x8000001F, device nodes, kernel log) and documented why no enclave comparison is possible on this machine | |
