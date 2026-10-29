#!/usr/bin/env python3
"""Plot ROB/PRF sweeps for every machine under data/ and locate each step.

Uses the newest data/<host>/rob_prf/sweep_*.csv per host (or the files given
on the command line). Writes analysis/out/rob_prf.png (one panel per host,
side by side) and analysis/out/rob_prf_steps.csv.

Step detection: the low plateau is the median cost of the smallest N values
(both misses overlap). The step is the first N where this point and the next
both exceed 1.5x that plateau (misses serialized, cost ~2x).
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
KINDS = ["nop", "int", "vec", "mix"]
LABELS = {"nop": "nop (ROB)", "int": "add r64 (int PRF)",
          "vec": "addps xmm (vector PRF)", "mix": "int/vec alternating"}


def load(path):
    rows = defaultdict(list)
    with open(path) as f:
        for r in csv.DictReader(f):
            # Prefer core cycles when perf was available, else TSC ticks.
            v = r["cycles_per_iter"] or r["tsc_per_iter"]
            rows[(r["kind"], int(r["n"]))].append(float(v))
    unit = "core cycles" if r["cycles_per_iter"] else "TSC ticks"
    curves = {}
    for kind in KINDS:
        ns = sorted(n for k, n in rows if k == kind)
        if ns:
            curves[kind] = (ns, [st.median(rows[(kind, n)]) for n in ns])
    return curves, unit


def find_step(ns, med, base_points=8):
    lo = st.median(med[:base_points])
    for i in range(len(ns) - 1):
        if med[i] > 1.5 * lo and med[i + 1] > 1.5 * lo:
            return ns[i]
    return None


def main():
    if len(sys.argv) > 1:
        files = [Path(p) for p in sys.argv[1:]]
    else:
        files = []
        for hostdir in sorted((ROOT / "data").iterdir()):
            sweeps = sorted((hostdir / "rob_prf").glob("sweep_*.csv"))
            if sweeps:
                files.append(sweeps[-1])
    if not files:
        sys.exit("no sweep data found under data/*/rob_prf/")

    OUT.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(1, len(files), figsize=(6.5 * len(files), 4.5),
                             squeeze=False, sharey=False)
    steps = []
    for ax, path in zip(axes[0], files):
        host = path.parent.parent.name
        curves, unit = load(path)
        for kind, (ns, med) in curves.items():
            step = find_step(ns, med)
            line, = ax.plot(ns, med, label=LABELS[kind], linewidth=1.4)
            if step is not None:
                ax.axvline(step, color=line.get_color(), linestyle=":", linewidth=1)
            steps.append({"host": host, "kind": kind, "step_n": step,
                          "file": str(path.relative_to(ROOT))})
            print(f"{host:15s} {kind:4s} step at N = {step}")
        ax.set_title(host)
        ax.set_xlabel("filler instructions between missing loads (N)")
        ax.set_ylabel(f"{unit} per iteration (median)")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(OUT / "rob_prf.png", dpi=150)

    with open(OUT / "rob_prf_steps.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["host", "kind", "step_n", "file"])
        w.writeheader()
        w.writerows(steps)
    print(f"wrote {OUT / 'rob_prf.png'} and {OUT / 'rob_prf_steps.csv'}")


if __name__ == "__main__":
    main()
