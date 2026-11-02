# Enclave comparison (planned)

The assignment requires each selected experiment to run both inside and
outside an enclave on the same enclave-capable machine, or a documented
reason why that isn't possible.

## Status

- `upgrade` (i7-8700): the CPU supports SGX1, but there's no `/dev/sgx_enclave`
  or `/dev/isgx`, so SGX is not enabled in firmware/kernel. Still need to find a
  course machine with SGX enabled.
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

## Design notes

- The ROB/PRF kernels (`build/rob_prf_kernels.S`) and the TLB kernel
  (`exp/tlb/tlb_chase.S`) are plain assembly, so the same objects can be
  linked into an enclave. The TLB `alias` pattern (one physical page mapped
  many times) relies on `mmap` of a memfd, which isn't available inside an
  enclave, so the in-enclave TLB run will use `private` + `dense` only. Enclaves can't easily use writable+executable pages, so the code
  can't be generated at runtime.
- `rdtsc`/`rdtscp` cause a fault inside SGX1 enclaves. Options: time each
  ECALL from outside the enclave with many iterations per call to amortize
  the transition cost, or use a counting thread. Whether SGX2 allows
  `rdtsc` depends on the CPU; check on the machine you use.
- Enclave memory (EPC) uses 4 KiB pages only, so Q9's in-enclave comparison
  will mostly be a documented limitation. EPC size also limits how large the
  pointer-chase buffers can be.
- Candidate SDKs: Intel SGX SDK, or Gramine (runs the unmodified binary
  inside an enclave, which is simpler, but `rdtsc` still needs handling).
