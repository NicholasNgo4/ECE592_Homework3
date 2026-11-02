/* Experiment core shared by the SGX enclave and its native twin; see core.h.
 * Uses only malloc/free/memcpy so it builds against the SGX trusted libc. */
#include "core.h"

#include <stdlib.h>
#include <string.h>

#include "hw3_common.h" /* xorshift64 */
#include "rob_prf.h"    /* generated kernel table */

#define LINE 64
#define PAGE 4096

uint64_t tlb_chase(void **addrs, uint64_t len, uint64_t total);

static uint64_t rng = 0xdeadbeefcafeULL;

/* Same random single-cycle linking as chase_build() in common/. */
static char *chase_link(size_t bytes, uint64_t seed)
{
    size_t lines = bytes / LINE;
    char *buf = core_alloc(bytes);
    size_t *order = malloc(lines * sizeof(*order));
    if (!buf || !order)
        return NULL;
    for (size_t i = 0; i < lines; i++)
        order[i] = i;
    uint64_t s = seed;
    for (size_t i = lines - 1; i > 0; i--) {
        size_t j = xorshift64(&s) % (i + 1);
        size_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (size_t i = 0; i < lines; i++)
        *(void **)(buf + order[i] * LINE) = buf + order[(i + 1) % lines] * LINE;
    free(order);
    return buf;
}

/* ---- Q4/Q5 ---- */

static char *rob_a, *rob_b;
static size_t rob_bytes;

int core_rob_setup(uint64_t mib)
{
    rob_bytes = (size_t)mib << 20;
    rob_a = chase_link(rob_bytes, 0x1234567ULL);
    rob_b = chase_link(rob_bytes, 0x7654321ULL);
    return rob_a && rob_b ? 0 : -1;
}

int core_rob_count(void)
{
    return rob_nkernels;
}

void core_rob_info(int k, char kind[8], int *n)
{
    strncpy(kind, rob_kernels[k].kind, 7);
    kind[7] = 0;
    *n = rob_kernels[k].n;
}

/* Start each run on a random line, as exp/rob_prf/main.c does. */
void core_rob_run(int k, uint64_t iters)
{
    size_t lines = rob_bytes / LINE;
    void **pa = (void **)(rob_a + (xorshift64(&rng) % lines) * LINE);
    void **pb = (void **)(rob_b + (xorshift64(&rng) % lines) * LINE);
    rob_kernels[k].fn(pa, pb, iters);
}

/* ---- Q6/Q8 ---- */

static char *tlb_priv, *tlb_dense;
static void **tlb_addrs;
static size_t *tlb_order;

int core_tlb_setup(uint64_t max_pages)
{
    tlb_priv = core_alloc(max_pages * PAGE);
    tlb_dense = core_alloc(max_pages * LINE);
    tlb_addrs = malloc(max_pages * sizeof(*tlb_addrs));
    tlb_order = malloc(max_pages * sizeof(*tlb_order));
    if (!tlb_priv || !tlb_dense || !tlb_addrs || !tlb_order)
        return -1;
    /* Fault in every page; slot offsets rotate as in exp/tlb/main.c. */
    memset(tlb_priv, 0, max_pages * PAGE);
    memset(tlb_dense, 0, max_pages * LINE);
    return 0;
}

void core_tlb_prepare(int pattern, uint64_t pages)
{
    for (size_t i = 0; i < pages; i++)
        tlb_order[i] = i;
    for (size_t i = pages - 1; i > 0; i--) {
        size_t j = xorshift64(&rng) % (i + 1);
        size_t t = tlb_order[i];
        tlb_order[i] = tlb_order[j];
        tlb_order[j] = t;
    }
    for (size_t i = 0; i < pages; i++) {
        size_t pg = tlb_order[i];
        tlb_addrs[i] = pattern == TLB_DENSE
                           ? tlb_dense + pg * LINE
                           : tlb_priv + pg * PAGE + (pg % (PAGE / LINE)) * LINE;
    }
    tlb_chase(tlb_addrs, pages, 2 * pages + 10000); /* warm TLB + caches */
}

void core_tlb_run(uint64_t pages, uint64_t total)
{
    tlb_chase(tlb_addrs, pages, total);
}
