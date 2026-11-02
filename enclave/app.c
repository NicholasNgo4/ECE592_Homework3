/* Driver for the enclave comparison. Built twice from this file:
 *   hw3_sgx     calls enclave/core.c inside an SGX enclave via ECALLs
 *   hw3_native  calls the same core.c directly in a normal process
 * Every measurement is timed here, outside the call, with the TSC, and the
 * median cost of an empty call (an ECALL round trip for hw3_sgx) is
 * subtracted. Timing outside works whether or not the CPU lets enclave
 * code execute RDTSC; `-t probe` tests that separately.
 *
 * CSV formats match exp/rob_prf and exp/tlb, plus a call_overhead_tsc
 * column, so the existing loaders can read them. */
#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw3_common.h"

#ifdef USE_SGX
#include "hw3_u.h"
#include "sgx_urts.h"
#define MODE "sgx"
static sgx_enclave_id_t eid;

static int start(void)
{
    sgx_status_t st = sgx_create_enclave("enclave.signed.so", 1, NULL, NULL, &eid, NULL);
    if (st != SGX_SUCCESS)
        fprintf(stderr, "sgx_create_enclave failed: 0x%04x\n", st);
    return st == SGX_SUCCESS ? 0 : -1;
}
static int rob_setup(uint64_t mib) { int r = -1; ecall_rob_setup(eid, &r, mib); return r; }
static int rob_count(void) { int r = 0; ecall_rob_count(eid, &r); return r; }
static void rob_info(int k, char *kind, int *n) { ecall_rob_info(eid, k, kind, n); }
static void rob_run(int k, uint64_t it) { ecall_rob_run(eid, k, it); }
static int tlb_setup(uint64_t m) { int r = -1; ecall_tlb_setup(eid, &r, m); return r; }
static void tlb_prepare(int p, uint64_t n) { ecall_tlb_prepare(eid, p, n); }
static void tlb_run(uint64_t n, uint64_t t) { ecall_tlb_run(eid, n, t); }
static void empty(void) { ecall_empty(eid); }
static int probe(uint64_t *d)
{
    sgx_status_t st = ecall_rdtsc_probe(eid, d);
    if (st != SGX_SUCCESS)
        fprintf(stderr, "RDTSCP inside the enclave failed: 0x%04x\n", st);
    return st == SGX_SUCCESS ? 0 : -1;
}
#else
#include <sys/mman.h>

#include "core.h"
#define MODE "native"

/* 4 KiB pages, like the enclave (EPC has no huge pages). */
void *core_alloc(size_t bytes)
{
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    madvise(p, bytes, MADV_NOHUGEPAGE);
    return p;
}
static int start(void) { return 0; }
static int rob_setup(uint64_t mib) { return core_rob_setup(mib); }
static int rob_count(void) { return core_rob_count(); }
static void rob_info(int k, char *kind, int *n) { core_rob_info(k, kind, n); }
static void rob_run(int k, uint64_t it) { core_rob_run(k, it); }
static int tlb_setup(uint64_t m) { return core_tlb_setup(m); }
static void tlb_prepare(int p, uint64_t n) { core_tlb_prepare(p, n); }
static void tlb_run(uint64_t n, uint64_t t) { core_tlb_run(n, t); }
static void __attribute__((noinline)) empty(void) { __asm__ volatile(""); }
static int probe(uint64_t *d)
{
    uint64_t t0 = tsc_now();
    *d = tsc_now() - t0;
    return 0;
}
#endif

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

/* Median TSC cost of an empty call. */
static uint64_t call_overhead(void)
{
    enum { N = 2001 };
    static uint64_t d[N];
    for (int i = 0; i < 100; i++)
        empty();
    for (int i = 0; i < N; i++) {
        uint64_t t0 = tsc_now();
        empty();
        d[i] = tsc_now() - t0;
    }
    qsort(d, N, sizeof(d[0]), cmp_u64);
    return d[N / 2];
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s -t rob|tlb|probe [-c cpu] [-o out.csv]\n"
            "          [-m MiB] [-i iters] [-M max_pages] [-L lo:hi:step] [-n accesses] [-r reps]\n"
            "  rob: -m chase buffer MiB (default 128), -i iterations (2000), -r reps (9)\n"
            "  tlb: -M largest page count (16384), -n loads per run (400000), -r reps (7),\n"
            "       -L linear sweep instead of the geometric one (as exp/tlb)\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *test = NULL, *outpath = NULL, *lin = NULL;
    int cpu = 2, reps = 0;
    uint64_t mib = 128, iters = 2000, max_pages = 16384, accesses = 400000;
    int opt;
    while ((opt = getopt(argc, argv, "t:c:o:m:i:M:L:n:r:h")) != -1) {
        switch (opt) {
        case 't': test = optarg; break;
        case 'c': cpu = atoi(optarg); break;
        case 'o': outpath = optarg; break;
        case 'm': mib = strtoull(optarg, NULL, 10); break;
        case 'i': iters = strtoull(optarg, NULL, 10); break;
        case 'M': max_pages = strtoull(optarg, NULL, 10); break;
        case 'L': lin = optarg; break;
        case 'n': accesses = strtoull(optarg, NULL, 10); break;
        case 'r': reps = atoi(optarg); break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }
    if (!test) {
        usage(argv[0]);
        return 1;
    }
    if (pin_to_cpu(cpu) != 0) {
        perror("pin_to_cpu");
        return 1;
    }
    if (start() != 0)
        return 1;

    if (!strcmp(test, "probe")) {
        uint64_t d;
        if (probe(&d) != 0)
            return 3;
        printf("%s: RDTSCP executed, back-to-back delta %llu ticks\n", MODE,
               (unsigned long long)d);
        return 0;
    }

    FILE *out = outpath ? fopen(outpath, "w") : stdout;
    if (!out) {
        perror(outpath);
        return 1;
    }
    double ghz = tsc_ghz();
    uint64_t ovh = call_overhead();
    fprintf(stderr, "%s, cpu %d, TSC %.3f GHz, empty-call overhead %llu ticks\n",
            MODE, cpu, ghz, (unsigned long long)ovh);

    if (!strcmp(test, "rob")) {
        if (rob_setup(mib) != 0) {
            fprintf(stderr, "rob setup failed (%llu MiB x 2)\n", (unsigned long long)mib);
            return 1;
        }
        reps = reps ? reps : 9;
        fprintf(out, "kind,n,rep,tsc_per_iter,cycles_per_iter,call_overhead_tsc\n");
        for (int k = 0, nk = rob_count(); k < nk; k++) {
            char kind[8];
            int n;
            rob_info(k, kind, &n);
            rob_run(k, 100); /* warm-up: code pages, loop branch */
            for (int r = 0; r < reps; r++) {
                uint64_t t0 = tsc_now();
                rob_run(k, iters);
                uint64_t t1 = tsc_now();
                fprintf(out, "%s,%d,%d,%.2f,,%llu\n", kind, n, r,
                        (double)(t1 - t0 - ovh) / iters, (unsigned long long)ovh);
            }
        }
    } else if (!strcmp(test, "tlb")) {
        if (tlb_setup(max_pages) != 0) {
            fprintf(stderr, "tlb setup failed (%llu pages)\n", (unsigned long long)max_pages);
            return 1;
        }
        reps = reps ? reps : 7;
        static const char *names[] = {"private", "dense"};
        fprintf(out, "page_size,pattern,pages,rep,tsc_per_access,cycles_per_access,"
                     "tsc_ghz,call_overhead_tsc\n");
        /* Same sweeps as exp/tlb: 16 points per doubling from 4, or linear. */
        unsigned long long lo = 0, hi = 0, step = 0;
        if (lin && (sscanf(lin, "%llu:%llu:%llu", &lo, &hi, &step) != 3 || !step || hi > max_pages)) {
            fprintf(stderr, "bad -L range (hi must be <= -M)\n");
            return 1;
        }
        uint64_t prev = 0;
        for (double p = lin ? lo : 4; p <= (lin ? hi : max_pages);
             p = lin ? p + step : p * 1.0442737824274138) {
            uint64_t pages = (uint64_t)(p + 0.5);
            if (pages == prev)
                continue;
            prev = pages;
            uint64_t total = accesses > 4 * pages ? accesses : 4 * pages;
            for (int pat = 0; pat < 2; pat++) {
                tlb_prepare(pat, pages);
                for (int r = 0; r < reps; r++) {
                    uint64_t t0 = tsc_now();
                    tlb_run(pages, total);
                    uint64_t t1 = tsc_now();
                    fprintf(out, "4k,%s,%llu,%d,%.3f,,%.4f,%llu\n", names[pat],
                            (unsigned long long)pages, r,
                            (double)(t1 - t0 - ovh) / total, ghz, (unsigned long long)ovh);
                }
            }
            fflush(out);
        }
    } else {
        usage(argv[0]);
        return 1;
    }
    if (out != stdout)
        fclose(out);
    return 0;
}
