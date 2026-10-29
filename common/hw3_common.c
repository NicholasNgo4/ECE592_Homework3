#define _GNU_SOURCE
#include "hw3_common.h"

#include <linux/perf_event.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LINE 64

int pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

int cycles_open(void)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
}

uint64_t cycles_read(int fd)
{
    uint64_t v = 0;
    if (read(fd, &v, sizeof(v)) != sizeof(v))
        return 0;
    return v;
}

char *chase_build(size_t bytes, uint64_t seed)
{
    size_t lines = bytes / LINE;
    char *buf = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED)
        return NULL;
    /* Huge pages cut TLB misses so the measured stall is dominated by DRAM,
     * not page walks. Best effort: ignored if THP is disabled. */
    madvise(buf, bytes, MADV_HUGEPAGE);

    size_t *order = malloc(lines * sizeof(*order));
    if (!order) {
        munmap(buf, bytes);
        return NULL;
    }
    for (size_t i = 0; i < lines; i++)
        order[i] = i;
    uint64_t s = seed ? seed : 0x9e3779b97f4a7c15ULL;
    for (size_t i = lines - 1; i > 0; i--) {
        size_t j = xorshift64(&s) % (i + 1);
        size_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (size_t i = 0; i < lines; i++) {
        void **cur = (void **)(buf + order[i] * LINE);
        *cur = buf + order[(i + 1) % lines] * LINE;
    }
    free(order);
    return buf;
}
