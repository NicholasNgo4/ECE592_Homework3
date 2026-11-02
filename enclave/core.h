/* Experiment core shared by the SGX enclave and its native twin.
 *
 * enclave/core.c is compiled twice: into the enclave (enclave.so, called
 * through ECALLs) and into a normal process (hw3_native). Both builds use
 * the same generated ROB/PRF kernels, the same TLB chase loop, the same
 * 4 KiB pages and the same pattern construction, so the only difference
 * between the two measurements is whether the code runs inside an enclave.
 * Timing is done by the caller (enclave/app.c), outside the call. */
#ifndef HW3_CORE_H
#define HW3_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Memory for the experiments, 4 KiB-page backed and page aligned.
 * Defined per build: enclave heap (EPC) or mmap with MADV_NOHUGEPAGE. */
void *core_alloc(size_t bytes);

/* Q4/Q5: two random pointer chases of `mib` MiB each, then kernel k. */
int core_rob_setup(uint64_t mib);
int core_rob_count(void);
void core_rob_info(int k, char kind[8], int *n);
void core_rob_run(int k, uint64_t iters);

/* Q6/Q8: one slot in each of `pages` pages, 4 KiB only.
 * pattern 0 = private (one slot per page), 1 = dense (consecutive lines). */
enum { TLB_PRIVATE = 0, TLB_DENSE = 1 };
int core_tlb_setup(uint64_t max_pages);
void core_tlb_prepare(int pattern, uint64_t pages); /* build order + warm */
void core_tlb_run(uint64_t pages, uint64_t total);

#endif
