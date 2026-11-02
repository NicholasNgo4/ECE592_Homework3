# AI-Use Log (Track B)

| Date | Tool | Task / artifact | Help received | What I verified and can explain |
|---|---|---|---|---|
| 2026-10-29 | Claude (Claude Code) | Question selection | Recommended Q4, Q5, Q6, Q8, Q9 (120 pts) and explained the trade-offs | |
| 2026-10-29 | Claude (Claude Code) | ARM scoping | Explained what an AArch64 port would require; decided to keep ARM out of scope | |
| 2026-10-29 | Claude (Claude Code) | `exp/tlb/`, `analysis/plot_tlb.py`, TLB parts of `run_all.sh` | Wrote the TLB kernel and driver (alias/private/dense patterns, verified 2 MiB pages), step and knee detection, and automatic refinement sweeps; found and fixed the L2-set-conflict and partial-THP artifacts; ran on `upgrade` | |
| 2026-10-29 | Claude (Claude Code) | Repo structure, `common/`, `exp/rob_prf/`, `scripts/`, `analysis/plot_rob_prf.py`, READMEs | Wrote the ROB/PRF kernel generator, sweep driver, run/plot scripts, and documentation; ran the first sweep on `upgrade` | |
