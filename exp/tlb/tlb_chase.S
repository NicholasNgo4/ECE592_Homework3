# uint64_t tlb_chase(void **addrs, uint64_t len, uint64_t total)
#
# Performs `total` loads, cycling through addrs[0..len-1]. Each load's
# address is addrs[i] + (value returned by the previous load). Every target
# slot holds 0, so the address is unchanged, but the add makes each load
# depend on the one before it: loads run one at a time, and the time per
# access is load latency plus any TLB miss penalty.
#
# The addrs[] reads themselves are sequential and independent of the chain,
# so the out-of-order core and prefetchers hide them.
#
# Kept as plain assembly (no JIT, no compiler involvement) so the same
# object can be linked into an SGX enclave.

.intel_syntax noprefix
.text
.globl tlb_chase
.type tlb_chase,@function
.p2align 6
tlb_chase:
    mov r9, rdx              # accesses remaining
    xor edx, edx             # value from previous load (always 0)
    xor ecx, ecx             # index into addrs
1:
    mov rax, [rdi + rcx*8]
    add rax, rdx
    mov rdx, [rax]
    inc rcx
    cmp rcx, rsi
    jne 2f
    xor ecx, ecx
2:
    dec r9
    jnz 1b
    mov rax, rdx
    ret
.size tlb_chase, .-tlb_chase

.section .note.GNU-stack,"",@progbits
