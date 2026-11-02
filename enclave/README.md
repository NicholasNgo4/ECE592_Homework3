# Enclave comparison

The assignment requires each selected experiment to run both inside and
outside an enclave on the same enclave-capable machine, or a documented
reason why that isn't possible.

## Where it ran: `artemisia`

| | |
|---|---|
| CPU | Intel Xeon Gold 5420+ (Sapphire Rapids, Golden Cove cores), 2 × 28 cores, microcode `0x2b000685` |
| OS | RHEL 9.8, Linux 6.17.8; in-kernel SGX driver (`/dev/sgx_enclave`), flexible launch control (`sgx_lc`) |
| EPC | two sections of about 1 GiB each (kernel log) |
| SGX software | Intel SGX SDK and PSW 2.23.100.2 (hardware mode) |
| Enclave | debug enclave (`DisableDebug=0`) signed with a locally generated key; heap 384 MiB; 1 TCS |
| CPU used | 12, whose SMT sibling (68) is offline, so no other thread shares the core |
| TSC | 2.000 GHz; `performance` governor |

Gramine 1.6.2 is also installed, but its Python package (`graminelibos`) is
missing, so its manifest and signing tools fail. Running the unmodified
binaries under Gramine was therefore not possible, and the comparison uses
the SDK directly.

## How it works

`core.c` holds the experiment code and is compiled twice, from the same
source, with the same generated ROB/PRF kernels (`build/rob_prf_kernels.S`)
and TLB loop (`exp/tlb/tlb_chase.S`):

- `enclave.signed.so` + `hw3_sgx`: `core.c` runs inside the enclave and is
  called through ECALLs (`hw3.edl`).
- `hw3_native`: `core.c` runs in a normal process.

Both builds use 4 KiB pages (enclave memory has no huge pages; the native
build uses `MADV_NOHUGEPAGE`), the same pattern construction and the same
random seeds, so the only difference is whether the code runs in the enclave.
`app.c` times every call from outside with the TSC and subtracts the median
cost of an empty call: about 9,600 ticks (4.8 µs) for an ECALL round trip,
42 ticks for a native call. Each timed call runs 2,000 ROB iterations
(~600k–1.4M ticks) or at least 400,000 TLB loads, so the subtraction is a
small correction.

`run_enclave.sh [cpu] [rounds]` builds everything, runs ROB/PRF and the
4 KiB TLB sweep in both modes for each round (alternating which mode goes
first), then linear refinements around the L1 and L2 TLB edges. Raw data:
`data/artemisia/enclave/`; plot: `python3 analysis/plot_enclave.py`.

What could not be done inside the enclave:
- 2 MiB pages (Q9): EPC has 4 KiB pages only.
- The TLB `alias` pattern: it maps one physical page many times with
  `mmap` of a memfd, which enclave memory cannot do. The TLB signal is
  `private − dense`, as on the other machines for 2 MiB pages.
- perf counters: not used in either mode, so both are timed with the TSC.

## Results (4 rounds per mode, 2026-11-02)

| | Native | SGX enclave | Published (Golden Cove) |
|---|---|---|---|
| RDTSCP inside the enclave | — | **allowed** (38-tick back-to-back delta) | — |
| `nop` step (ROB) | 496–500 | 496–500 | ROB 512 |
| `add` step (int PRF) | 216 | 216 | 280 int registers |
| `addps` step (vec PRF) | 244–248 | **260** | 332 vector registers |
| alternating step | 428 | 432 | — |
| ROB iteration before / after the `nop` step | ~276 / ~560–590 ticks | ~335–345 / ~690–705 ticks (**+20–22%**) | — |
| L1 DTLB (refined knee) | 97 | 96 | 96 |
| L2 TLB onset (refined) | 1720–1744 (extrapolates to 1618) | 1552–1576 (extrapolates to 1586) | 2048 (secondary source) |
| L1 miss, L2 TLB hit (`private − dense`, 128 pages) | +5.1 ticks | +5.1 ticks | +7 cycles |
| Beyond the L2 TLB (`private − dense`, 4096 pages) | 17.3 ticks | **38.5** | — |
| Beyond the L2 TLB (16384 pages) | 18.2 ticks | **73.5** | — |
| `dense` control (cache only) | identical in both modes at every point | | — |

Interpretation:
- **Core resources are the same inside an enclave.** The ROB and integer
  PRF steps do not move.
- **Vector PRF step moves 16 later inside the enclave (244–248 → 260).**
  On Ice Lake server, touching any of the AVX-512 registers ZMM16–31
  reserves 16 extra physical vector registers, and Golden Cove may do the
  same (Chips and Cheese). A normal Linux process touches ZMM16–31 through
  glibc's AVX-512 string functions; enclave code here does not, and
  EENTER starts from clean extended state. Hypothesis, not tested
  directly.
- **DRAM accesses are 20–22% slower inside the enclave**: both ROB plateaus
  rise, consistent with SGX memory encryption on every cache miss to EPC.
  Cache hits are not slower: the `dense` curves are identical.
- **Page walks are 2–4× more expensive inside the enclave**, and the
  penalty grows with the number of pages. TLB hits cost the same. This is
  consistent with SGX checking each new translation against the EPCM
  (Enclave Page Cache Map), whose entries also have to be cached.

## Machines where it was not possible

- `upgrade` (i7-8700): the CPU supports SGX1, but there's no `/dev/sgx_enclave`
  or `/dev/isgx`, so SGX is not enabled in firmware/kernel.
- `skylark` (AMD EPYC 7532, Zen 2): **an enclave run is not possible.**
  - No SGX: it is an Intel-only feature (no `sgx` flag, no `/dev/sgx*`).
  - AMD's closest analogue is SEV, which the silicon supports: CPUID
    `0x8000001F` EAX = `0x1000f` (SME, SEV, SEV-ES). But it is switched off:
    the kernel log reports `ccp 0000:22:00.1: SEV: memory encryption not
    enabled by BIOS`, the `sev` cpuinfo flag is absent, and there is no
    `/dev/sev` or `/dev/kvm`. Enabling it needs BIOS access (admin only).
  - Even if enabled, SEV is not an enclave. It encrypts a whole virtual
    machine's memory and needs KVM plus root to launch a guest, so the
    comparison would be "encrypted VM vs. plain VM", not "inside vs. outside
    an enclave" within one process.
  - Evidence: the SEV section of `data/skylark/machine_info.txt`, recorded
    by `scripts/machine_info.sh`.
