/* Trusted side: ECALL entry points and the enclave's core_alloc(). */
#include <stdlib.h>

#include "core.h"
#include "hw3_common.h"
#include "hw3_t.h"

/* Enclave heap pages are 4 KiB EPC pages; align to a page like mmap would. */
void *core_alloc(size_t bytes)
{
    char *raw = malloc(bytes + 4096);
    return raw ? (void *)(((uintptr_t)raw + 4095) & ~(uintptr_t)4095) : NULL;
}

int ecall_rob_setup(uint64_t mib) { return core_rob_setup(mib); }
int ecall_rob_count(void) { return core_rob_count(); }
void ecall_rob_info(int k, char *kind, int *n) { core_rob_info(k, kind, n); }
void ecall_rob_run(int k, uint64_t iters) { core_rob_run(k, iters); }

int ecall_tlb_setup(uint64_t max_pages) { return core_tlb_setup(max_pages); }
void ecall_tlb_prepare(int pattern, uint64_t pages) { core_tlb_prepare(pattern, pages); }
void ecall_tlb_run(uint64_t pages, uint64_t total) { core_tlb_run(pages, total); }

void ecall_empty(void) {}

uint64_t ecall_rdtsc_probe(void)
{
    uint64_t t0 = tsc_now();
    uint64_t t1 = tsc_now();
    return t1 - t0;
}
