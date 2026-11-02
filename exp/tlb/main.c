/* TLB sweep driver (Q6 levels/sizes, Q8 miss cost, Q9 page sizes).
 *
 * Touches one 64-byte slot in each of P pages, in random order, as a chain
 * of dependent loads (tlb_chase.S), and sweeps P. Each time P passes the
 * reach of a TLB level, the time per access steps up.
 *
 * Patterns (one CSV row per measurement, tagged with the pattern):
 *   alias   4 KiB only. Every virtual page maps the SAME physical page
 *           (one memfd page mapped P times), so the data always sits in
 *           64 L1-resident lines and only the translations vary. This is
 *           the clean TLB signal: no cache effect is possible.
 *   private One slot in each of P distinct physical pages of the chosen
 *           size (4k, 2m via THP, 1g via hugetlbfs). TLB + cache effects.
 *   dense   Control: P slots packed into consecutive lines of a few huge
 *           pages. Same data footprint as `private` but almost no TLB
 *           demand, so it shows the cache-only part of the cost.
 * Slot offset within a page rotates (page i uses line i mod lines-per-page).
 * For 4 KiB pages this spreads slots over all L1 sets; for 2 MiB pages it
 * also spreads them over L2/L3 sets exactly like the dense control does.
 * (A fixed or 4 KiB-limited offset in 2 MiB pages piles every slot into a
 * few L2 sets and produces a conflict-miss step that looks like a TLB.)
 *
 * -e adds raw PMU events (e.g. DTLB miss counters) read around each timed
 * repetition, so every row also carries event counts per access. This is
 * independent evidence that a timing step is a TLB step. */
#define _GNU_SOURCE
#include <getopt.h>
#include <linux/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hw3_common.h"

#define LINE 64
#define SZ_4K (4096UL)
#define SZ_2M (2UL << 20)
#define SZ_1G (1UL << 30)
#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif
#define MAX_EVENTS 8

uint64_t tlb_chase(void **addrs, uint64_t len, uint64_t total);

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-s 4k|2m|1g] [-c cpu] [-M max_pages] [-L lo:hi:step]\n"
            "          [-r reps] [-n accesses] [-e name=cfg,...] [-o out.csv]\n"
            "  -s size   page size under test (default 4k)\n"
            "  -c cpu    logical CPU to pin to (default 2)\n"
            "  -M pages  largest page count (default 4k:16384, 2m:2048, 1g:8)\n"
            "  -L range  linear sweep lo:hi:step instead of the default\n"
            "            geometric sweep (16 points per doubling); use to refine\n"
            "  -r reps   measurements per point (default 7)\n"
            "  -n n      loads per measurement (default 400000)\n"
            "  -e events raw PMU events to count per access, as name=config\n"
            "            (AMD config = umask << 8 | event, e.g. l1miss=0xff45);\n"
            "            adds <name>_per_access columns and a pmu_muxed flag\n"
            "  -o file   CSV output (default stdout)\n",
            prog);
}

static size_t slot_offset(size_t page, size_t psize)
{
    return psize == LINE ? 0 : (page % (psize / LINE)) * LINE;
}

/* Map P separate virtual 4 KiB pages onto one physical page. */
static char *map_alias(size_t pages)
{
    int fd = memfd_create("hw3_tlb_alias", 0);
    if (fd < 0 || ftruncate(fd, SZ_4K) != 0)
        return NULL;
    char *base = mmap(NULL, pages * SZ_4K, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED)
        return NULL;
    for (size_t i = 0; i < pages; i++) {
        void *p = mmap(base + i * SZ_4K, SZ_4K, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_FIXED | MAP_POPULATE, fd, 0);
        if (p == MAP_FAILED) {
            perror("mmap alias page (vm.max_map_count?)");
            return NULL;
        }
    }
    close(fd);
    return base;
}

/* Anonymous memory backed by pages of `psize`, aligned to `psize`. */
static char *map_private(size_t bytes, size_t psize)
{
    if (psize == SZ_1G) {
        char *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB,
                       -1, 0);
        return p == MAP_FAILED ? NULL : p;
    }
    size_t extra = psize == SZ_2M ? SZ_2M : 0;
    char *raw = mmap(NULL, bytes + extra, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (raw == MAP_FAILED)
        return NULL;
    char *p = (char *)(((uintptr_t)raw + extra) & ~(uintptr_t)(psize - 1));
    madvise(p, bytes, psize == SZ_2M ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
    return p;
}

/* AnonHugePages for this process, in kB, to verify THP actually backed the
 * 2 MiB experiment (THP is best effort and can silently fall back). */
static long anon_huge_kb(void)
{
    FILE *f = fopen("/proc/self/smaps_rollup", "r");
    char line[256];
    long kb = -1;
    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "AnonHugePages: %ld kB", &kb) == 1)
            break;
    fclose(f);
    return kb;
}

/* Map up to `want` 2 MiB pages one at a time, keeping only those confirmed
 * to be THP-backed (AnonHugePages grows by 2 MiB when the page is touched).
 * Pages that fall back to 4 KiB are unmapped and retried elsewhere, so the
 * sweep never mixes page sizes. Tries at most 2*want pages. */
static char **map_2m_verified(size_t want, size_t *got)
{
    size_t tries = 2 * want, n = 0, rejected = 0;
    char *res = mmap(NULL, (tries + 1) * SZ_2M, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    char **pages = malloc(want * sizeof(*pages));
    if (res == MAP_FAILED || !pages)
        return NULL;
    char *cur = (char *)(((uintptr_t)res + SZ_2M) & ~(uintptr_t)(SZ_2M - 1));
    long kb = anon_huge_kb();
    for (size_t t = 0; t < tries && n < want; t++, cur += SZ_2M) {
        if (mmap(cur, SZ_2M, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
            break;
        madvise(cur, SZ_2M, MADV_HUGEPAGE);
        cur[slot_offset(n, SZ_2M)] = 0;
        long now = anon_huge_kb();
        if (now - kb < 2048 && madvise(cur, SZ_2M, MADV_COLLAPSE) == 0)
            now = anon_huge_kb();
        if (now - kb >= 2048) {
            pages[n++] = cur;
            kb = now;
        } else {
            /* Not huge: release the 4 KiB pages and leave a hole. */
            mmap(cur, SZ_2M, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
            rejected++;
        }
    }
    fprintf(stderr, "THP check: %zu verified 2 MiB pages of %zu requested "
            "(%zu fell back to 4 KiB and were discarded)\n", n, want, rejected);
    *got = n;
    return pages;
}

static void shuffle(size_t *a, size_t n, uint64_t *rng)
{
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = xorshift64(rng) % (i + 1);
        size_t t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static size_t *sweep_points(size_t max, const char *lin, size_t *count)
{
    size_t *pts = malloc(4096 * sizeof(*pts));
    size_t n = 0;
    if (lin) {
        size_t lo, hi, step;
        if (sscanf(lin, "%zu:%zu:%zu", &lo, &hi, &step) != 3 || step == 0)
            return NULL;
        for (size_t p = lo; p <= hi && p <= max && n < 4096; p += step)
            pts[n++] = p;
    } else {
        /* 16 points per doubling from 4 pages up to max. */
        for (double p = 4; p <= max && n < 4096; p *= 1.0442737824274138) {
            size_t v = (size_t)(p + 0.5);
            if (n == 0 || v != pts[n - 1])
                pts[n++] = v;
        }
    }
    *count = n;
    return pts;
}

struct ctx {
    FILE *out;
    const char *size_name;
    int cfd, reps;
    uint64_t accesses;
    double ghz;
    void **addrs;
    size_t *order;
    uint64_t rng;
    int nev, efd[MAX_EVENTS];
    char *ename[MAX_EVENTS];
};

/* Parse "name=config,name=config" into c->ename / c->efd. */
static int open_events(struct ctx *c, char *spec)
{
    for (char *tok = strtok(spec, ","); tok; tok = strtok(NULL, ",")) {
        char *eq = strchr(tok, '=');
        if (!eq || c->nev == MAX_EVENTS)
            return -1;
        *eq = 0;
        int fd = raw_open(strtoull(eq + 1, NULL, 0));
        if (fd < 0) {
            perror("perf_event_open (raw event)");
            return -1;
        }
        c->ename[c->nev] = tok;
        c->efd[c->nev++] = fd;
    }
    return 0;
}

/* bases[i] = base + i*stride, for regions mapped as one contiguous block. */
static char **linear_bases(char *base, size_t n, size_t stride)
{
    char **b = malloc(n * sizeof(*b));
    for (size_t i = 0; b && i < n; i++)
        b[i] = base + i * stride;
    return b;
}

/* Visit one slot in each of bases[0..pages-1], in random order, and time
 * it. psize == LINE means dense (each base is itself a line); otherwise the
 * slot is at slot_offset() within a page of size psize. */
static void measure(struct ctx *c, const char *pattern, size_t pages,
                    char **bases, size_t psize)
{
    for (size_t i = 0; i < pages; i++)
        c->order[i] = i;
    shuffle(c->order, pages, &c->rng);
    for (size_t i = 0; i < pages; i++) {
        size_t pg = c->order[i];
        c->addrs[i] = bases[pg] + slot_offset(pg, psize);
    }
    uint64_t total = c->accesses > 4 * pages ? c->accesses : 4 * pages;
    tlb_chase(c->addrs, pages, 2 * pages + 10000); /* warm TLB + caches */
    for (int r = 0; r < c->reps; r++) {
        uint64_t e0[MAX_EVENTS], e1[MAX_EVENTS];
        int muxed = 0;
        for (int k = 0; k < c->nev; k++)
            e0[k] = raw_read(c->efd[k], &muxed);
        uint64_t c0 = c->cfd >= 0 ? cycles_read(c->cfd) : 0;
        uint64_t t0 = tsc_now();
        tlb_chase(c->addrs, pages, total);
        uint64_t t1 = tsc_now();
        uint64_t c1 = c->cfd >= 0 ? cycles_read(c->cfd) : 0;
        for (int k = 0; k < c->nev; k++)
            e1[k] = raw_read(c->efd[k], &muxed);
        fprintf(c->out, "%s,%s,%zu,%d,%.3f,", c->size_name, pattern, pages, r,
                (double)(t1 - t0) / total);
        if (c->cfd >= 0)
            fprintf(c->out, "%.3f", (double)(c1 - c0) / total);
        fprintf(c->out, ",%.4f", c->ghz);
        for (int k = 0; k < c->nev; k++)
            fprintf(c->out, ",%.4f", (double)(e1[k] - e0[k]) / total);
        if (c->nev)
            fprintf(c->out, ",%d", muxed);
        fprintf(c->out, "\n");
    }
    fflush(c->out);
}

int main(int argc, char **argv)
{
    const char *size_name = "4k", *lin = NULL, *outpath = NULL;
    char *events = NULL;
    int cpu = 2, reps = 7;
    long max_pages = 0;
    uint64_t accesses = 400000;
    int opt;
    while ((opt = getopt(argc, argv, "s:c:M:L:r:n:e:o:h")) != -1) {
        switch (opt) {
        case 's': size_name = optarg; break;
        case 'c': cpu = atoi(optarg); break;
        case 'M': max_pages = atol(optarg); break;
        case 'L': lin = optarg; break;
        case 'r': reps = atoi(optarg); break;
        case 'n': accesses = strtoull(optarg, NULL, 10); break;
        case 'e': events = optarg; break;
        case 'o': outpath = optarg; break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }

    size_t psize;
    if (!strcmp(size_name, "4k")) {
        psize = SZ_4K;
        if (!max_pages) max_pages = 16384;
    } else if (!strcmp(size_name, "2m")) {
        psize = SZ_2M;
        if (!max_pages) max_pages = 2048;
    } else if (!strcmp(size_name, "1g")) {
        psize = SZ_1G;
        if (!max_pages) max_pages = 8;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (pin_to_cpu(cpu) != 0) {
        perror("pin_to_cpu");
        return 1;
    }
    char **priv;
    if (psize == SZ_2M) {
        size_t got;
        priv = map_2m_verified((size_t)max_pages, &got);
        if (!priv || got == 0) {
            fprintf(stderr, "UNSUPPORTED: no THP-backed 2 MiB pages available\n");
            return 2;
        }
        max_pages = (long)got; /* sweep only verified huge pages */
    } else {
        char *base = map_private((size_t)max_pages * psize, psize);
        if (!base) {
            fprintf(stderr, "UNSUPPORTED: cannot allocate %ld x %s pages (%s)\n",
                    max_pages, size_name,
                    psize == SZ_1G ? "no 1 GiB hugetlbfs pool reserved?" : "out of memory?");
            return 2;
        }
        /* Fault in every slot with a write, so each page gets its own
         * physical page rather than the shared zero page. */
        for (long i = 0; i < max_pages; i++)
            base[i * psize + slot_offset(i, psize)] = 0;
        priv = linear_bases(base, (size_t)max_pages, psize);
    }

    size_t npts;
    size_t *pts = sweep_points((size_t)max_pages, lin, &npts);
    if (!pts || npts == 0) {
        fprintf(stderr, "bad sweep range\n");
        return 1;
    }

    char **alias = NULL;
    if (psize == SZ_4K) {
        char *a = map_alias((size_t)max_pages);
        if (!a)
            return 1;
        alias = linear_bases(a, (size_t)max_pages, SZ_4K);
    }

    /* Dense control: max_pages consecutive lines in 2 MiB-backed memory. */
    size_t dense_bytes = ((size_t)max_pages * LINE + SZ_2M - 1) & ~(SZ_2M - 1);
    char *dense_base = map_private(dense_bytes, SZ_2M);
    if (!dense_base)
        return 1;
    memset(dense_base, 0, dense_bytes);
    char **dense = linear_bases(dense_base, (size_t)max_pages, LINE);

    struct ctx c = {
        .size_name = size_name, .reps = reps, .accesses = accesses,
        .addrs = malloc(max_pages * sizeof(void *)),
        .order = malloc(max_pages * sizeof(size_t)),
        .rng = 0x5eed1234abcdULL,
    };
    c.out = outpath ? fopen(outpath, "w") : stdout;
    if (!c.out || !c.addrs || !c.order) {
        perror("setup");
        return 1;
    }
    if (events && open_events(&c, events) != 0) {
        fprintf(stderr, "bad -e event list\n");
        return 1;
    }
    c.cfd = cycles_open();
    c.ghz = tsc_ghz();
    fprintf(stderr, "cpu %d, page size %s, %zu points up to %ld pages, TSC %.3f GHz, %s\n",
            cpu, size_name, npts, max_pages, c.ghz,
            c.cfd >= 0 ? "recording perf core cycles" : "no perf access, TSC only");

    fprintf(c.out, "page_size,pattern,pages,rep,tsc_per_access,cycles_per_access,tsc_ghz");
    for (int k = 0; k < c.nev; k++)
        fprintf(c.out, ",%s_per_access", c.ename[k]);
    fprintf(c.out, "%s\n", c.nev ? ",pmu_muxed" : "");
    for (size_t k = 0; k < npts; k++) {
        size_t p = pts[k];
        if (alias)
            measure(&c, "alias", p, alias, SZ_4K);
        measure(&c, "private", p, priv, psize);
        measure(&c, "dense", p, dense, LINE);
    }
    if (c.out != stdout)
        fclose(c.out);
    return 0;
}
