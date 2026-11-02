# ECE 592 Homework 3 — Microarchitecture Reverse Engineering (Track B)

Track B: individual work, AI use permitted and disclosed (see [AI_LOG.md](AI_LOG.md)).
Due **November 3, 2026, 11:59 PM Eastern**. Assignment: [Homework_3.pdf](Homework_3.pdf).

## Selected questions — 120 points (100 required)

| Q# | Question | Points | Subsystem | Status |
|---|---|---|---|---|
| 4 | Reverse engineer the ROB size | 30 | OoO back end | Harness working; `upgrade` done |
| 5 | Reverse engineer the physical register file size; identify the register class | 35 | OoO back end / renaming | Harness working; `upgrade` done |
| 6 | Reverse engineer the number of TLB levels and the size of each level | 35 | Virtual memory | Harness working; `upgrade` done |
| 8 | Measure the cost of a TLB miss | 10 | Virtual memory | Harness working; `upgrade` done |
| 9 | Compare TLB behavior across supported page sizes | 10 | Virtual memory | 4 KiB + 2 MiB done on `upgrade`; 1 GiB unavailable (no pool) |
| | **Total** | **120** | | |

Stretch goal if time allows: **Q10, BTB capacity (20 pts)**, which would add the front end.

**Out of scope:** ARM (Thunderbird, Neoverse N1). Track B only needs two
machines per question, and the x86 machines are enough for both the
requirement and the optional bonus sweep. Porting would need an AArch64
kernel generator and a `CNTVCT_EL0` timer (25 MHz on Thunderbird), and ARM
has no SGX equivalent for the enclave comparison.

### Why this combination

- **Q4 + Q5** use one technique with different filler instructions, so the
  report can show how the ROB and the separate integer and vector physical
  register files each bound the out-of-order window. That gives depth on
  register renaming, not just a single number.
- **Q6 + Q8 + Q9** cover address translation (TLB hierarchy, page-walk
  cost, 4 KiB / 2 MiB / 1 GiB pages) with one harness. The main
  methodological problem is separating TLB effects from cache effects, since
  cache reverse engineering is out of scope for this assignment.
- Together they cover two separate subsystems (core back end and memory
  translation) instead of several variations on one structure.

## Requirements checklist (Track B)

For **every** selected question:

- [ ] Run on **two** course machines and present results side by side
- [ ] Explain the method and any differences between the machines
- [ ] Include a code snippet and the methodology (what is tested, how, what is measured, how it supports the conclusion)
- [ ] Repeated measurements and plots
- [ ] Keep experimental findings separate from published specifications
- [ ] **Enclave comparison:** run inside and outside an enclave (e.g. Intel SGX) on the same enclave-capable machine, or document why that isn't possible
- [ ] Identify the CPU model and microarchitecture of each machine; document unsupported features and access limits

Report front page must list the selected questions and the total points (120).
Submit a ZIP containing the PDF report, source code, raw data, plotting scripts,
reproduction instructions, and the AI-use log.

## Machines

| Host | CPU | Microarchitecture | SGX | perf counters | Huge pages | Role |
|---|---|---|---|---|---|---|
| upgrade | Intel Core i7-8700 | Coffee Lake (Skylake family) | No `/dev/sgx` (not enabled) | No (`perf_event_paranoid=4`) | 2 MiB via THP; no 1 GiB pool | Machine 1 |
| _TBD_ | | | | | | Machine 2 (suggestion: Skylark, AMD EPYC 7532 / Zen 2, for a cross-vendor comparison) |
| _TBD_ | | | Needs SGX enabled | | | Enclave comparison |

`data/<host>/machine_info.txt` holds the full record for each machine
(lscpu, microcode, kernel, SGX, perf access, huge-page pools, SMT, load).

## Experiments

### Q4 / Q5 — ROB and physical register file ([exp/rob_prf/](exp/rob_prf/))

Method (Henry Wong, 2013): each loop iteration does two independent
cache-missing loads separated by N filler instructions. While the window
holds both loads, their misses overlap and an iteration costs about one
miss latency. Once the fillers use up some resource, the second load can't
issue until the first retires, and the cost jumps to about two miss
latencies. The filler type decides which resource runs out first:

| Filler | Resource it consumes | Measures |
|---|---|---|
| `nop` | ROB entry only | ROB size (Q4) |
| `add r64, r64` | ROB entry + integer physical register | Integer PRF (Q5) |
| `addps xmm, xmm` | ROB entry + vector/FP physical register | Vector PRF (Q5) |
| alternating int / vec | Splits demand across both register files | Shows the register files are separate (Q5 register class) |

Reading the results: the step for a register-writing filler occurs at
about (PRF size − registers already holding committed or in-flight state),
so the reported PRF size adds back the architectural register count. The
int and vec curves stepping well before the `nop` curve shows the register
files, not the ROB, are the limit. The mixed curve stepping later than
either one alone shows they are separate pools.

Results on `upgrade` (two independent runs, median of 5 and 9 reps, TSC ticks):

| Filler | Step at N | Published spec (Skylake family) |
|---|---|---|
| nop | ~220 | ROB 224 |
| int | ~144–148 | 180 integer physical registers |
| vec | ~144–148 | 168 vector physical registers |
| mix | ~208 | — |

Open questions for the report: noise in the `nop` curve between N≈110 and
260 (front end or uop-cache effects as the loop body grows?), and the exact
offset between each step and the PRF size.

### Q6 / Q8 / Q9 — TLB ([exp/tlb/](exp/tlb/))

A chain of dependent loads touches one 64-byte slot in each of P pages, in
random order, and P is swept. Each time P passes a TLB level's reach, the
time per access steps up. Three patterns separate TLB effects from cache
effects:

| Pattern | What it is | Purpose |
|---|---|---|
| `alias` (4 KiB) | P virtual pages all mapped to **one** physical page, so the data is always in L1 | Pure TLB signal: no cache effect is possible |
| `private` | One slot in each of P separate physical pages | Realistic case: TLB + cache |
| `dense` | P slots in consecutive lines | Control: same data footprint, almost no TLB demand |

For 2 MiB pages the alias trick isn't possible (shared-memory huge pages are
disabled), so the TLB signal is `private − dense`. Every 2 MiB page is
checked to be a real huge page before use; pages the kernel backs with
4 KiB pages are discarded so page sizes never mix. `run_all.sh` does a
coarse geometric sweep, then a dense linear sweep around each detected step.

Results on `upgrade` (TSC 3.19 GHz):

| Level | 4 KiB pages | 2 MiB pages | Published (Skylake family) |
|---|---|---|---|
| L1 DTLB entries (Q6) | **64** (flat to 64, rises at 65) | **32** (flat to 32, rises at 33) | 64 (4K), 32 (2M) |
| L2 TLB / STLB entries (Q6) | **~1530** (ramp extrapolates to 1529) | **~1530** (extrapolates to 1531) | 1536, shared 4K/2M |
| Access cost, L1 TLB hit | 1.37 ns | 1.34 ns | — |
| Added cost of an L1 TLB miss that hits the L2 TLB (Q8) | +2.0 ns | +2.0 ns | — |
| Added cost of an L2 TLB miss, page walk with cached page tables (Q8) | +3.0 ns | +3.5 ns | — |
| 1 GiB pages (Q9) | — | — | Unavailable: no 1 GiB hugetlbfs pool, needs root |

Findings so far:
- **Two TLB levels.** The same ~1536-entry L2 TLB holds both 4 KiB and 2 MiB
  entries (the same step for both sizes). The L1 DTLB has separate capacities
  per page size (64 vs 32).
- **The cache confound is real.** In the `private` 4 KiB curve, a step at
  about 512 pages matches the `dense` control exactly: that's the 32 KiB L1D
  filling (512 lines), not a TLB. The `alias` curve has no step there.
- **Huge-page reach.** With 2 MiB pages, the L2 TLB covers about 3 GiB
  instead of 6 MiB.
- With 4 KiB `private` pages beyond the L2 TLB, the cost keeps climbing
  (up to ~90 ticks at 16384 pages) because the page-table entries
  themselves stop fitting in cache. Not analyzed further, since that's a
  cache effect.

### Enclave comparison ([enclave/](enclave/))

Planned. See [enclave/README.md](enclave/README.md).

## Repository layout

```
common/          shared helpers: rdtscp timing, TSC calibration, CPU pinning, perf cycles, pointer-chase buffers
exp/rob_prf/     Q4/Q5: kernel generator (gen_kernels.py) + sweep driver (main.c)
exp/tlb/         Q6/Q8/Q9: dependent-load kernel (tlb_chase.S) + sweep driver (main.c)
enclave/         SGX wrapper for the same kernels (planned)
scripts/         run_all.sh (build + run everything), machine_info.sh
data/<host>/     raw CSVs, run logs, machine_info.txt; one folder per machine
analysis/        plot_rob_prf.py, plot_tlb.py; output in analysis/out/
report/          report source
```

## Reproducing on a new machine

```bash
git clone https://github.com/NicholasNgo4/ECE592_Homework3.git
cd ECE592_Homework3
scripts/run_all.sh 2          # pin to logical CPU 2; pick an idle core (~30 s)
python3 analysis/plot_rob_prf.py && python3 analysis/plot_tlb.py
git add data/$(hostname -s) analysis/out && git commit -m "Add $(hostname -s) data" && git push
```

Requirements: x86-64 Linux, gcc, python3 with matplotlib (`pip install --user
matplotlib` if missing). The plotting scripts pick up every `data/<host>/`
folder automatically, so each machine added becomes another side-by-side
panel. Page sizes the machine can't provide are logged as `UNSUPPORTED` in
`data/<host>/run_<stamp>.log` rather than failing the run.

Tips:
- Check `uptime` and `top` first, because other students share these
  machines. Pick a core that isn't busy, and re-run if the load is high.
- The 2 MiB TLB sweep needs about 4 GiB of free RAM. Use `build/tlb -s 2m -M 1024` on smaller machines.
- Pass `ROB_MAX=800` to `make` (after `make clean`) for cores whose ROB may be larger than 640, such as Sapphire Rapids.
- Larger TLBs (Zen 2, Sapphire Rapids) may need `build/tlb -s 4k -M 32768`.
