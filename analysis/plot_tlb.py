#!/usr/bin/env python3
"""Plot TLB sweeps for every machine under data/ and locate TLB levels.

Plots the most complete data/<host>/tlb/<size>_*.csv per host and page size
(largest page count, newest on ties), so a run cut short by too few
verified huge pages does not replace a full one.
Writes analysis/out/tlb.png (rows = hosts, columns = page sizes) and
analysis/out/tlb_levels.csv.

Which curve measures the TLB:
  4k  `alias`: data is always L1-resident, so every step is a TLB step.
  2m  `private` minus `dense`: subtracting the same-footprint control
      removes the cache-only cost and leaves the translation cost.
Step detection walks the curve keeping a running plateau (median of the
last few points); a step is found where this point and the next both exceed
the plateau by STEP_REL, then walked back to its onset (the first point
more than ONSET_REL above the plateau). Each plateau's level is reported
in TSC ticks and ns, so level differences give TLB miss costs (Q8).
"""
import csv
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis" / "out"
SIZES = ["4k", "2m", "1g"]
STEP_REL = 0.25   # a step is a >25% rise over the current plateau
STEP_ABS = 0.5    # ...and at least 0.5 ticks, to ignore noise near the floor
WINDOW = 4


def load(path):
    rows = defaultdict(list)
    ghz = None
    with open(path) as f:
        for r in csv.DictReader(f):
            rows[(r["pattern"], int(r["pages"]))].append(float(r["tsc_per_access"]))
            ghz = float(r["tsc_ghz"])
    curves = {}
    for pat in {p for p, _ in rows}:
        ns = sorted(n for p, n in rows if p == pat)
        curves[pat] = (ns, [st.median(rows[(pat, n)]) for n in ns])
    return curves, ghz


def tlb_signal(size, curves):
    """The curve whose steps are attributable to the TLB alone."""
    if size == "4k" and "alias" in curves:
        return "alias", curves["alias"]
    ns, priv = curves["private"]
    dense = dict(zip(*curves["dense"]))
    base = min(dense.values())
    # Add the floor back so levels stay in absolute ticks per access.
    return "private-dense", (ns, [p - dense[n] + base for n, p in zip(ns, priv)])


SETTLE = 3        # points after a step treated as transition, not plateau
ONSET_REL = 0.05  # a point has left the plateau once >5% (and 0.2 ticks) above it
ONSET_ABS = 0.2


def find_levels(ns, med):
    """Return [(last_n_before, first_n, level)] plateaus. A step is
    bracketed by the last point on the old plateau and the first point
    above it; the first plateau has last_n_before = None."""
    starts = [0]
    i = WINDOW
    while i < len(ns) - 1:
        lo = starts[-1] + (SETTLE if len(starts) > 1 else 0)
        plateau = st.median(med[max(lo, i - WINDOW):i])
        thr = max(plateau * (1 + STEP_REL), plateau + STEP_ABS)
        if med[i] > thr and med[i + 1] > thr:
            # Capacity is where the rise begins, not where it crosses the
            # threshold: set-associative TLBs fill gradually past capacity.
            onset = max(lo + 1, i - WINDOW)
            for j in range(i - 1, lo, -1):
                if med[j] <= plateau * (1 + ONSET_REL) + ONSET_ABS:
                    onset = j + 1
                    break
            i = onset
            starts.append(i)
            i += SETTLE + WINDOW  # judge the next step against a settled plateau
        else:
            i += 1
    levels = []
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else len(ns)
        settled = med[s + (SETTLE if k else 0):e] or med[s:e]
        levels.append((ns[s - 1] if k else None, ns[s], st.median(settled)))
    return levels


def newest(hostdir, size):
    files = sorted((hostdir / "tlb").glob(f"{size}_*.csv"))
    return files[-1] if files else None


def most_complete(hostdir, size):
    """Sweep reaching the most pages, newest on ties (for plotting only:
    run_all's refinement ranges must come from the run just made)."""
    def reach(path):
        with open(path) as f:
            return max(int(r["pages"]) for r in csv.DictReader(f))
    files = sorted((hostdir / "tlb").glob(f"{size}_*.csv"))
    return max(files, key=lambda f: (reach(f), f.name)) if files else None


def print_refine_ranges(host):
    """Print `size lo:hi:step` for a dense linear sweep around every step
    found in the newest coarse sweeps; scripts/run_all.sh feeds these back
    to build/tlb -L."""
    for size in SIZES:
        path = newest(ROOT / "data" / host, size)
        if path is None:
            continue
        curves, _ = load(path)
        _, (ns, sig) = tlb_signal(size, curves)
        for before, n, _ in find_levels(ns, sig)[1:]:
            lo = max(1, int(before * 0.9))
            hi = int(n * 1.1) + 1
            step = max(1, (hi - lo) // 80)
            print(f"{size} {lo}:{hi}:{step}")


def knee(ns, sig):
    """Locate the single step in a refinement sweep.

    The running-window detector above misses gradual ramps (the window
    rises with the ramp), so here the plateau is fixed: the median of the
    first quarter of the range, which run_all centres on one step. Onset is
    the first of 3 consecutive points above the plateau. The capacity
    estimate extrapolates a least-squares line through the ramp (onset up
    to halfway to the top) back down to the plateau level.
    Returns (last_flat_n, onset_n, extrapolated_capacity) or None."""
    q = max(3, len(ns) // 4)
    plateau = st.median(sig[:q])
    thr = plateau * (1 + ONSET_REL) + ONSET_ABS
    onset = next((i for i in range(q, len(ns) - 2)
                  if all(v > thr for v in sig[i:i + 3])), None)
    if onset is None:
        return None
    top = max(sig[onset:])
    half = plateau + 0.5 * (top - plateau)
    end = next((i for i in range(onset, len(ns)) if sig[i] >= half), len(ns) - 1)
    xs, ys = ns[onset:end + 1], sig[onset:end + 1]
    if len(xs) < 2:
        return ns[onset - 1], ns[onset], None
    mx, my = st.mean(xs), st.mean(ys)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / \
        sum((x - mx) ** 2 for x in xs)
    cap = mx - (my - plateau) / slope if slope > 0 else None
    return ns[onset - 1], ns[onset], cap


def refined_steps(hostdir, size):
    """Knee of each refinement sweep for this page size."""
    out = []
    for path in sorted((hostdir / "tlb").glob(f"refine_{size}_*.csv")):
        curves, _ = load(path)
        _, (ns, sig) = tlb_signal(size, curves)
        k = knee(ns, sig)
        if k:
            out.append((path, *k))
    return out


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--ranges":
        print_refine_ranges(sys.argv[2])
        return
    hosts = [h for h in sorted((ROOT / "data").iterdir())
             if (h / "tlb").is_dir()] if len(sys.argv) < 2 else \
            [ROOT / "data" / h for h in sys.argv[1:]]
    grid = [(h, [most_complete(h, s) for s in SIZES]) for h in hosts]
    grid = [(h, fs) for h, fs in grid if any(fs)]
    if not grid:
        sys.exit("no TLB data found under data/*/tlb/")
    cols = [s for i, s in enumerate(SIZES) if any(fs[i] for _, fs in grid)]

    OUT.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(len(grid), len(cols), squeeze=False,
                             figsize=(6 * len(cols), 4.2 * len(grid)))
    out_rows = []
    for row, (host, files) in enumerate(grid):
        for col, size in enumerate(cols):
            ax = axes[row][col]
            path = files[SIZES.index(size)]
            if path is None:
                ax.set_axis_off()
                ax.set_title(f"{host.name} {size}: no data")
                continue
            curves, ghz = load(path)
            for pat in ("alias", "private", "dense"):
                if pat in curves:
                    ax.plot(*curves[pat], label=pat, linewidth=1.3)
            sig_name, (ns, sig) = tlb_signal(size, curves)
            levels = find_levels(ns, sig)
            prev = None
            for k, (before, n, lvl) in enumerate(levels):
                if k:
                    ax.axvline(n, color="gray", linestyle=":", linewidth=1)
                out_rows.append({
                    "host": host.name, "page_size": size, "signal": sig_name,
                    "level": k, "step_after_pages": before, "starts_at_pages": n,
                    "ticks_per_access": round(lvl, 2),
                    "ns_per_access": round(lvl / ghz, 2),
                    "added_ns_vs_prev": "" if prev is None else round((lvl - prev) / ghz, 2),
                    "extrapolated_entries": "",
                    "file": str(path.relative_to(ROOT)),
                })
                prev = lvl
                where = f"step in ({before}, {n}] pages" if k else "base"
                print(f"{host.name:12s} {size} level {k}: {where:26s} "
                      f"{lvl:6.2f} ticks = {lvl / ghz:5.2f} ns")
            for rpath, before, n, cap in refined_steps(host, size):
                out_rows.append({
                    "host": host.name, "page_size": size,
                    "signal": sig_name + " (refined)", "level": "",
                    "step_after_pages": before, "starts_at_pages": n,
                    "ticks_per_access": "", "ns_per_access": "",
                    "added_ns_vs_prev": "",
                    "extrapolated_entries": "" if cap is None else round(cap),
                    "file": str(rpath.relative_to(ROOT)),
                })
                capstr = "" if cap is None else f", ramp extrapolates to {cap:.0f} entries"
                print(f"{host.name:12s} {size} refined: onset in ({before}, {n}] pages{capstr}")
            ax.set_xscale("log", base=2)
            ax.set_title(f"{host.name} — {size} pages (steps from {sig_name})")
            ax.set_xlabel("pages touched (one line each)")
            ax.set_ylabel("TSC ticks per access (median)")
            ax.grid(alpha=0.3, which="both")
            ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(OUT / "tlb.png", dpi=150)
    with open(OUT / "tlb_levels.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out_rows[0]))
        w.writeheader()
        w.writerows(out_rows)
    print(f"wrote {OUT / 'tlb.png'} and {OUT / 'tlb_levels.csv'}")


if __name__ == "__main__":
    main()
