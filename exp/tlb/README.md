# Q6 / Q8 / Q9 — TLB experiments (planned)

## Q6: number of TLB levels and size of each (35 pts)

Pointer chase that touches **one line per page** across P pages, sweeping P.
Each time the page count passes a TLB level's reach, cycles per access
should step up: L1 DTLB miss + L2 hit costs a few cycles, and an L2 TLB miss
costs a page walk.

Controls needed to separate TLB effects from cache effects (cache reverse
engineering is out of scope, so this has to be done carefully):
- Vary the line offset inside each page (page i uses line i mod 64) so the
  touched lines spread across cache sets instead of all landing in the same set.
- Keep the total touched data (P × 64 B) small compared with the caches near
  each expected TLB boundary, so a step can't be explained by data falling
  out of a cache.
- Random page order so the next-page prefetcher can't help.

## Q8: cost of a TLB miss (10 pts)

Difference in cycles per access between the plateaus (L1 TLB hit, L2 TLB
hit, page walk), using the same data. Optionally make the page-table walk
itself miss by using a very large footprint.

## Q9: page sizes (10 pts)

Repeat Q6 with 2 MiB pages (`MAP_HUGETLB`, or THP via `madvise`) and 1 GiB
pages where available (`pdpe1gb` flag plus a reserved 1 GiB pool). Compare
entry counts and reach per page size, and check whether the TLB is split or
shared between page sizes.

On `upgrade`: THP mode is `madvise`, the hugetlbfs pools are empty
(reserving pages needs root), and the CPU supports 1 GiB pages. Explicit
huge pages may therefore need `madvise` THP for 2 MiB, and 1 GiB may not be
possible without an admin. Document this as an access limitation if so.
