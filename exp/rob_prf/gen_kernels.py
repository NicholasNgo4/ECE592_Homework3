#!/usr/bin/env python3
"""Generate the ROB / physical-register-file test kernels (Henry Wong method).

Each kernel is  uint64_t k(void **chainA, void **chainB, uint64_t iters):

    loop:
        rax = [rax]          ; cache-missing load, chain A
        N filler instructions
        rcx = [rcx]          ; cache-missing load, chain B (independent of A)
        N filler instructions
        dec rdx / jnz loop

While the N fillers plus the next load fit in the out-of-order window, the
two misses overlap and an iteration costs ~1 miss latency. Once the filler
exhausts some resource, the second miss cannot enter the window until the
first retires, and the cost jumps to ~2 miss latencies. Which resource runs
out depends on the filler:

    nop  - occupies a ROB entry, writes no register     -> ROB size
    int  - add r64,r64: allocates an integer phys reg   -> integer PRF
    vec  - addps xmm,xmm: allocates a vector/FP phys reg -> vector PRF
    mix  - alternating int / vec                         -> whichever binds first

Kernels are generated ahead of time (not JIT'd) so the same objects can be
linked into an SGX enclave, where writable+executable pages are unavailable.
"""
import argparse
from pathlib import Path

# Registers free inside the kernel: rax/rcx hold the chase pointers, rdx is
# the loop counter. rsi/rdi are free after being copied. All caller-saved.
INT_REGS = ["r8", "r9", "r10", "r11", "rsi", "rdi"]
VEC_REGS = [f"xmm{i}" for i in range(16)]


def filler(kind, count, start):
    """Return `count` filler lines; `start` continues register rotation."""
    out = []
    for i in range(start, start + count):
        if kind == "nop":
            out.append("nop")
        elif kind == "int":
            r = INT_REGS[i % len(INT_REGS)]
            out.append(f"add {r}, {r}")
        elif kind == "vec":
            r = VEC_REGS[i % len(VEC_REGS)]
            out.append(f"addps {r}, {r}")
        elif kind == "mix":
            if i % 2 == 0:
                r = INT_REGS[(i // 2) % len(INT_REGS)]
                out.append(f"add {r}, {r}")
            else:
                r = VEC_REGS[(i // 2) % len(VEC_REGS)]
                out.append(f"addps {r}, {r}")
    return out


def kernel(kind, n):
    name = f"k_{kind}_{n}"
    lines = [
        f".globl {name}",
        f".type {name},@function",
        ".p2align 6",
        f"{name}:",
        "mov rax, rdi",
        "mov rcx, rsi",
    ]
    if kind in ("vec", "mix"):
        # Start from 0.0 so addps never produces denormals (microcode assists).
        lines += [f"xorps {r}, {r}" for r in VEC_REGS]
    lines += [".p2align 6", "1:", "mov rax, [rax]"]
    lines += filler(kind, n, 0)
    lines += ["mov rcx, [rcx]"]
    lines += filler(kind, n, n)
    lines += ["dec rdx", "jnz 1b", "ret", f".size {name}, .-{name}", ""]
    return name, "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--max", type=int, default=640, help="largest N")
    ap.add_argument("--step", type=int, default=4)
    ap.add_argument("--kinds", default="nop,int,vec,mix")
    args = ap.parse_args()

    out = Path(args.outdir)
    out.mkdir(parents=True, exist_ok=True)
    kinds = args.kinds.split(",")
    names = []
    asm = [".intel_syntax noprefix", ".text", ""]
    for kind in kinds:
        for n in range(0, args.max + 1, args.step):
            name, body = kernel(kind, n)
            names.append((kind, n, name))
            asm.append(body)
    asm.append('.section .note.GNU-stack,"",@progbits')
    (out / "rob_prf_kernels.S").write_text("\n".join(asm) + "\n")

    c = ['#include "rob_prf.h"', ""]
    c += [f"extern uint64_t {name}(void **, void **, uint64_t);" for _, _, name in names]
    c += ["", "const struct rob_kernel rob_kernels[] = {"]
    c += [f'    {{"{kind}", {n}, {name}}},' for kind, n, name in names]
    c += ["};", f"const int rob_nkernels = {len(names)};", ""]
    (out / "rob_prf_table.c").write_text("\n".join(c))


if __name__ == "__main__":
    main()
