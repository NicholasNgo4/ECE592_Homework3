# Q6 / Q8 / Q9 — TLB experiments

`tlb_chase.S` performs a chain of dependent loads over a list of
addresses; `main.c` builds the address lists and sweeps the page count P.
Results and interpretation are in the top-level README.

```
build/tlb -s 4k|2m|1g [-c cpu] [-M max_pages] [-L lo:hi:step] [-r reps] [-n loads] [-o out.csv]
```

Output columns: `page_size,pattern,pages,rep,tsc_per_access,cycles_per_access,tsc_ghz`
(`cycles_per_access` is empty when perf counters are unavailable).

## Method

- **One slot per page, random order, dependent loads.** Each load's address
  is `addrs[i] + (value of previous load)`. The value is always 0, so the
  add only creates a dependence: loads run one at a time, and time per
  access = load latency + translation penalty. The `addrs[]` reads are
  sequential and independent, so the out-of-order core hides them.
- **Slot offset rotates within each page** (page i uses line
  i mod lines-per-page). For 4 KiB pages this spreads slots over all 64 L1
  sets. For 2 MiB pages it also spreads them over L2/L3 sets like the dense
  control. An early version used only the first 4 KiB of each 2 MiB page,
  which put every slot in 64 of the 1024 L2 sets and produced a
  conflict-miss step that looked like a TLB step.

## Separating TLB from cache effects (required: cache RE is out of scope)

| Pattern | Data footprint | TLB demand | Shows |
|---|---|---|---|
| `alias` (4 KiB only) | 64 lines in **one** physical page, always L1 | P translations | TLB only |
| `private` | P lines in P physical pages | P translations | TLB + cache |
| `dense` | P consecutive lines in 2 MiB pages | ~0 | Cache only |

`alias` maps one memfd page at P virtual addresses (P separate VMAs, so
`vm.max_map_count`, default 65530, limits P). 2 MiB alias isn't possible
because shmem THP is disabled (`shmem_enabled=never`), so for 2 MiB the TLB
signal is `private − dense`.

## Huge-page verification (Q9)

THP is best effort. On a fragmented machine, the kernel silently backs part
of a region with 4 KiB pages: 60–85% coverage in early tests. In the first
2 MiB run that made the "L2 TLB" step coincide with the point where coverage
ran out. So the driver now maps 2 MiB pages one at a time, checks that
`AnonHugePages` grew by 2 MiB (trying `MADV_COLLAPSE` if not), and discards
pages that didn't become huge. Only verified pages are swept, and the count
is logged.

1 GiB pages need a reserved hugetlbfs pool (root). Without one the driver
exits with status 2 and `run_all.sh` logs `UNSUPPORTED`.

## Analysis (`analysis/plot_tlb.py`)

- Coarse sweep: running-plateau step detector gives levels and plateau
  costs (Q8 = differences between plateaus, in ns).
- Refinement sweeps (`refine_<size>_*.csv`): fixed plateau from the first
  quarter of the range; onset = first of 3 consecutive points above it;
  capacity = linear fit of the ramp extrapolated to the plateau. The ramp
  past capacity is gradual rather than a cliff, consistent with a
  set-associative TLB whose replacement isn't strict LRU.
