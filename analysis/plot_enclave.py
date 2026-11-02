#!/usr/bin/env python3
"""Plot the enclave comparison (enclave/run_enclave.sh) for every host with
data/<host>/enclave/ data.

Pools all rounds per mode (native = normal process, sgx = inside an SGX
enclave; same code, same 4 KiB pages, same CPU) and writes
analysis/out/enclave.png and analysis/out/enclave_summary.csv:
  left   ROB/PRF: TSC ticks per iteration vs filler count, per filler kind
  right  TLB: private - dense (translation cost) vs pages touched
"""
import csv
import statistics as st
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).resolve().parent))
from plot_rob_prf import find_step  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "analysis" / "out"
MODES = {"native": "-", "sgx": "--"}
MODE_LABEL = {"native": "native", "sgx": "SGX enclave"}
# Fixed categorical order; color follows the filler kind (ROB) or mode (TLB).
KIND_COLORS = {"nop": "#2a78d6", "int": "#eb6834", "vec": "#1baf7a", "mix": "#eda100"}
MODE_COLORS = {"native": "#2a78d6", "sgx": "#eb6834"}
INK = "#52514e"


def pooled(paths, key_cols, val_col):
    rows = defaultdict(list)
    for path in paths:
        with open(path) as f:
            for r in csv.DictReader(f):
                rows[tuple(r[c] for c in key_cols)].append(float(r[val_col]))
    return {k: st.median(v) for k, v in rows.items()}


def style(ax):
    ax.grid(True, color="#e4e3df", linewidth=0.6)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    ax.tick_params(colors=INK)


def main():
    hosts = [d for d in sorted((ROOT / "data").iterdir()) if (d / "enclave").is_dir()]
    if not hosts:
        raise SystemExit("no data/<host>/enclave/ data (run enclave/run_enclave.sh)")
    fig, axes = plt.subplots(len(hosts), 2, figsize=(13, 4.8 * len(hosts)), squeeze=False)
    summary = []
    for row, hostdir in enumerate(hosts):
        edir = hostdir / "enclave"
        ax = axes[row][0]
        for mode, ls in MODES.items():
            files = sorted(edir.glob(f"rob_{mode}_*.csv"))
            med = pooled(files, ("kind", "n"), "tsc_per_iter")
            for kind, color in KIND_COLORS.items():
                ns = sorted(int(n) for k, n in med if k == kind)
                ys = [med[(kind, str(n))] for n in ns]
                step = find_step(ns, ys)
                ax.plot(ns, ys, color=color, linestyle=ls, linewidth=2 if ls == "-" else 1.6,
                        label=f"{kind} ({MODE_LABEL[mode]}): step {step}")
                summary.append({"host": hostdir.name, "test": "rob", "mode": mode,
                                "kind": kind, "step_n": step, "rounds": len(files)})
        ax.set_title(f"{hostdir.name}: ROB / PRF, native (solid) vs enclave (dashed)", fontsize=10)
        ax.set_xlabel("filler instructions between missing loads (N)")
        ax.set_ylabel("TSC ticks per iteration (median of all rounds)")
        ax.legend(frameon=False, fontsize=7, ncol=2)
        style(ax)

        ax = axes[row][1]
        for mode in MODES:
            files = sorted(edir.glob(f"tlb_{mode}_*.csv"))
            med = pooled(files, ("pattern", "pages"), "tsc_per_access")
            ns = sorted(int(n) for p, n in med if p == "private")
            sig = [med[("private", str(n))] - med[("dense", str(n))] for n in ns]
            dense = [med[("dense", str(n))] for n in ns]
            ax.plot(ns, sig, color=MODE_COLORS[mode], linewidth=2,
                    label=f"private − dense ({MODE_LABEL[mode]})")
            ax.plot(ns, dense, color=MODE_COLORS[mode], linewidth=1, linestyle=":",
                    label=f"dense control ({MODE_LABEL[mode]})")
            for n in (128, 1024, 4096, 16384):
                if ("private", str(n)) in med:
                    summary.append({"host": hostdir.name, "test": "tlb", "mode": mode,
                                    "kind": f"private-dense@{n}",
                                    "step_n": round(med[("private", str(n))] - med[("dense", str(n))], 2),
                                    "rounds": len(files)})
        ax.set_xscale("log", base=2)
        ax.set_title(f"{hostdir.name}: 4 KiB TLB, native vs enclave", fontsize=10)
        ax.set_xlabel("pages touched (one line each)")
        ax.set_ylabel("TSC ticks per access (median of all rounds)")
        ax.legend(frameon=False, fontsize=8, loc="upper left")
        style(ax)

    fig.tight_layout()
    OUT.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT / "enclave.png", dpi=150)
    with open(OUT / "enclave_summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["host", "test", "mode", "kind", "step_n", "rounds"])
        w.writeheader()
        w.writerows(summary)
    print(f"wrote {OUT / 'enclave.png'} and {OUT / 'enclave_summary.csv'}")


if __name__ == "__main__":
    main()
