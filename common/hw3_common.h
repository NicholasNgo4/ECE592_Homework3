/* Shared helpers for every HW3 experiment: timing, CPU pinning, and
 * pointer-chase buffers that miss in every cache level. */
#ifndef HW3_COMMON_H
#define HW3_COMMON_H

#include <stddef.h>
#include <stdint.h>

/* Serializing timestamp read (rdtscp + lfence). Counts TSC ticks, which
 * run at a fixed reference frequency, not at the core clock. */
static inline uint64_t tsc_now(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtscp\n\tlfence" : "=a"(lo), "=d"(hi) : : "rcx", "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t xorshift64(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

/* Pin the calling thread to one logical CPU. Returns 0 on success. */
int pin_to_cpu(int cpu);

/* Core-cycle counter via perf_event_open (user-mode cycles only).
 * cycles_open() returns -1 if perf is unavailable (e.g. perf_event_paranoid
 * too high); callers then fall back to the TSC. */
int cycles_open(void);
uint64_t cycles_read(int fd);

/* Allocate `bytes` and link one pointer per 64-byte line into a single
 * random cycle, so each dependent load goes to a line the prefetchers
 * cannot predict. Returns the buffer base; since every line is on the
 * cycle, any line address is a valid starting point (use chase_start).
 * The mapping is intentionally never freed: experiments run one sweep and
 * exit. */
char *chase_build(size_t bytes, uint64_t seed);

/* A random line of the chain, so successive measurements start on cold
 * lines instead of re-walking (and caching) the same prefix. */
static inline void **chase_start(char *base, size_t bytes, uint64_t r)
{
    return (void **)(base + (r % (bytes / 64)) * 64);
}

#endif
