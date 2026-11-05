# Report

`report.tex` is the Track B report (front page lists Q4, Q5, Q6, Q8, Q9 = 120 points).

Compile from this directory (run twice so cross-references resolve):

```bash
cd report
pdflatex report.tex && pdflatex report.tex
```

Figures are read from `../analysis/out/`; regenerate them first if the data changed:

```bash
python3 analysis/plot_rob_prf.py && python3 analysis/plot_tlb.py && python3 analysis/plot_tlb_pmu.py && python3 analysis/plot_enclave.py
```

Needs the `booktabs`, `tabularx`, `listings`, `caption`, `enumitem` and `hyperref`
packages (all in a standard TeX Live install, and on Overleaf).
