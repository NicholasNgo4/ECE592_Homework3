#!/usr/bin/env python3
"""Plot TLB sweeps that carry DTLB event counts (scripts/tlb_pmu.sh).

Uses the newest data/<host>/tlb/pmu_<size>_*.csv per host and page size.
Writes analysis/out/tlb_pmu.png: one column per page size, top row = core
cycles per access, bottom row = L2 TLB misses (page walks) per access, on a
shared page-count axis. Lining the two rows up shows which timing steps
coincide with real TLB misses and which do not (cache-side effects).
"""
import csv
import statistics as st
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis" / "out"
SIZES = ["4k", "2m"]
# Fixed categorical order: color follows the pattern in every panel.
COLORS = {"alias": "#2a78d6", "private": "#eb6834", "dense": "#1baf7a"}
INK = "#52514e"


def load(path):
    rows = defaultdict(lambda: defaultdict(list))
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in ("cycles_per_access", "l1miss_per_access", "l2miss_per_access"):
                rows[r["pattern"]][(int(r["pages"]), k)].append(float(r[k]))
    curves = {}
    for pat, d in rows.items():
        ns = sorted({n for n, _ in d})
        curves[pat] = (ns, {k: [st.median(d[(n, k)]) for n in ns]
                            for k in ("cycles_per_access", "l1miss_per_access",
                                      "l2miss_per_access")})
    return curves


def main():
    panels = []
    for hostdir in sorted((ROOT / "data").iterdir()):
        for size in SIZES:
            files = sorted((hostdir / "tlb").glob(f"pmu_{size}_*.csv"))
            if files:
                panels.append((hostdir.name, size, files[-1]))
    if not panels:
        raise SystemExit("no pmu_*.csv data (run scripts/tlb_pmu.sh)")

    fig, axes = plt.subplots(2, len(panels), figsize=(6 * len(panels), 6.5),
                             sharex="col", squeeze=False,
                             gridspec_kw={"height_ratios": [3, 2]})
    for col, (host, size, path) in enumerate(panels):
        curves = load(path)
        top, bot = axes[0][col], axes[1][col]
        for pat in ("alias", "private", "dense"):
            if pat not in curves:
                continue
            ns, m = curves[pat]
            top.plot(ns, m["cycles_per_access"], color=COLORS[pat],
                     linewidth=2, label=pat)
            if pat != "dense":  # dense has no TLB misses by construction
                bot.plot(ns, m["l2miss_per_access"], color=COLORS[pat],
                         linewidth=2, label=f"{pat}: L2 TLB misses")
                bot.plot(ns, m["l1miss_per_access"], color=COLORS[pat],
                         linewidth=1, linestyle="--", label=f"{pat}: L1 DTLB misses")
        for ax in (top, bot):
            ax.set_xscale("log", base=2)
            ax.grid(True, color="#e4e3df", linewidth=0.6)
            for spine in ("top", "right"):
                ax.spines[spine].set_visible(False)
            ax.tick_params(colors=INK)
            # Published Zen 2 capacities, for reference only.
            for n in (64, 2048):
                ax.axvline(n, color=INK, linestyle=":", linewidth=0.8)
        top.set_title(f"{host}: {size} pages ({path.name})", fontsize=10)
        top.set_ylabel("core cycles per access (median)")
        bot.set_ylabel("events per access (median)")
        bot.set_xlabel("pages touched (one line each); dotted = 64 and 2048")
        bot.set_ylim(-0.05, 1.1)
        top.legend(frameon=False, fontsize=8)
        bot.legend(frameon=False, fontsize=8, loc="upper left")

    fig.tight_layout()
    OUT.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT / "tlb_pmu.png", dpi=150)
    print(f"wrote {OUT / 'tlb_pmu.png'}")


if __name__ == "__main__":
    main()
