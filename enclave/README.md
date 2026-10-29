# Enclave comparison (planned)

The assignment requires each selected experiment to run both inside and
outside an enclave on the same enclave-capable machine, or a documented
reason why that isn't possible.

## Status

- `upgrade` (i7-8700): the CPU supports SGX1, but there's no `/dev/sgx_enclave`
  or `/dev/isgx`, so SGX is not enabled in firmware/kernel. Still need to find a
  course machine with SGX enabled.

## Design notes

- The ROB/PRF kernels are generated ahead of time as plain assembly
  (`build/rob_prf_kernels.S`) so the same object can be linked into an
  enclave. Enclaves can't easily use writable+executable pages, so the code
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
