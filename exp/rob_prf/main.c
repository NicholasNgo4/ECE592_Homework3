/* ROB / physical register file sweep driver.
 *
 * For every generated kernel (filler kind x N) this runs `reps` timed
 * measurements of `iters` loop iterations and writes one CSV row per
 * measurement. The step in cycles/iteration vs N marks the resource size;
 * analysis/plot_rob_prf.py finds it. */
#define _GNU_SOURCE
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hw3_common.h"
#include "rob_prf.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-c cpu] [-k kinds] [-r reps] [-i iters] [-m MiB] [-o out.csv]\n"
            "  -c cpu    logical CPU to pin to (default 2)\n"
            "  -k kinds  comma list of nop,int,vec,mix (default all)\n"
            "  -r reps   measurements per (kind, N) (default 9)\n"
            "  -i iters  loop iterations per measurement (default 2000)\n"
            "  -m MiB    size of each pointer-chase buffer (default 256)\n"
            "  -o file   CSV output (default stdout)\n",
            prog);
}

static int kind_selected(const char *kinds, const char *kind)
{
    if (!kinds)
        return 1;
    size_t len = strlen(kind);
    for (const char *p = kinds; (p = strstr(p, kind)); p += len) {
        int start_ok = (p == kinds || p[-1] == ',');
        int end_ok = (p[len] == '\0' || p[len] == ',');
        if (start_ok && end_ok)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int cpu = 2, reps = 9;
    long iters = 2000, mib = 256;
    const char *kinds = NULL, *outpath = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "c:k:r:i:m:o:h")) != -1) {
        switch (opt) {
        case 'c': cpu = atoi(optarg); break;
        case 'k': kinds = optarg; break;
        case 'r': reps = atoi(optarg); break;
        case 'i': iters = atol(optarg); break;
        case 'm': mib = atol(optarg); break;
        case 'o': outpath = optarg; break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }

    if (pin_to_cpu(cpu) != 0) {
        perror("pin_to_cpu");
        return 1;
    }
    FILE *out = outpath ? fopen(outpath, "w") : stdout;
    if (!out) {
        perror(outpath);
        return 1;
    }

    size_t bytes = (size_t)mib << 20;
    char *a = chase_build(bytes, 0x1234567ULL);
    char *b = chase_build(bytes, 0x7654321ULL);
    if (!a || !b) {
        fprintf(stderr, "failed to allocate chase buffers\n");
        return 1;
    }

    int cfd = cycles_open();
    fprintf(stderr, "cpu %d, %s\n", cpu,
            cfd >= 0 ? "recording perf core cycles and TSC"
                     : "no perf access, recording TSC only");
    fprintf(out, "kind,n,rep,tsc_per_iter,cycles_per_iter\n");

    uint64_t rng = 0xdeadbeefcafeULL;
    for (int k = 0; k < rob_nkernels; k++) {
        const struct rob_kernel *kn = &rob_kernels[k];
        if (!kind_selected(kinds, kn->kind))
            continue;
        /* Warm-up: fault in code pages and train the loop branch. */
        kn->fn(chase_start(a, bytes, xorshift64(&rng)),
               chase_start(b, bytes, xorshift64(&rng)), 100);
        for (int r = 0; r < reps; r++) {
            void **pa = chase_start(a, bytes, xorshift64(&rng));
            void **pb = chase_start(b, bytes, xorshift64(&rng));
            uint64_t c0 = cfd >= 0 ? cycles_read(cfd) : 0;
            uint64_t t0 = tsc_now();
            kn->fn(pa, pb, (uint64_t)iters);
            uint64_t t1 = tsc_now();
            uint64_t c1 = cfd >= 0 ? cycles_read(cfd) : 0;
            double tpi = (double)(t1 - t0) / iters;
            if (cfd >= 0)
                fprintf(out, "%s,%d,%d,%.2f,%.2f\n", kn->kind, kn->n, r, tpi,
                        (double)(c1 - c0) / iters);
            else
                fprintf(out, "%s,%d,%d,%.2f,\n", kn->kind, kn->n, r, tpi);
        }
    }
    if (out != stdout)
        fclose(out);
    return 0;
}
