# ============================================================
# Tagless two-word node representation (see x64.s), without the
# identity bootstrap: the first parsed tree becomes the accumulator.
# Behavior is undefined for fewer than 2 input trees.
#
#   Every node is exactly two i32 words, [u][v] (8 bytes).
#     u == 0            -> leaf   (v ignored; the canonical leaf is [0][0])
#     u != 0, v == 0    -> stem(u)
#     u != 0, v != 0    -> fork(u, v)
#
# Registers:
#   rbx = leaf address (= heap base, permanent)
#   rdi = free pointer (permanent, absolute)
#   rbp = &apply (call *%rbp = 2B vs call rel32 = 5B)
#
# Build (Linux):   gcc -nostdlib -static main.s -o main
# Build (macOS):   clang -nostdlib main.s -o main
# ============================================================

#ifdef __APPLE__
  .equ SYS_EXIT,   0x2000001
  .equ SYS_READ,   0x2000003
  .equ SYS_WRITE,  0x2000004
#else
  .equ SYS_EXIT,   60
  .equ SYS_READ,   0
  .equ SYS_WRITE,  1
#endif

.text
.globl _start
#ifdef __APPLE__
.globl start
start:
#endif

_start:
    ## rbp = &apply; rbx = leaf = heap base, derived from rbp as a disp8
    ## (leaf-apply; apply is the last function). All PC-relative.
    leaq    apply(%rip), %rbp   # rbp = &apply
    .byte   0x8d, 0x5d          # leal disp8(%rbp), %ebx  (ModRM 5d: reg=ebx, base=rbp)
    .byte   leaf-apply          # disp8 = sizeof(apply)
    leal    8(%rbx), %edi       # rdi = heap free pointer (past the leaf node)

    ## No identity — the first parsed tree becomes the accumulator.
    call    parse_tree
    pushq   %rax

1:  call    parse_tree
    popq    %rdx                # accumulator (pop before the EOF test; pop leaves flags)
    js      2f
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  ## Fold loop done; rbp (= &apply) is dead. Retarget it to emit_tree so
    ## the call here and emit's self-recursion are both 2-byte `call *%rbp`.
    .byte   0x8d, 0x6d          # leal disp8(%rbp), %ebp  (ModRM 6d: reg=ebp, base=rbp)
    .byte   emit_tree-apply     # disp8 = emit_tree - apply
    call    *%rbp               # emit_tree(edx = accumulator)
    movb    $60, %al            # SYS_EXIT
    xorl    %edi, %edi
    syscall

## ---- I/O: shared syscall stub ----
write_byte:
    push    $1
    pop     %rax                # rax=1=SYS_WRITE
do_io:
    pushq   %rdi                # save free pointer
    movl    %eax, %edi          # fd = eax
    push    %rcx                # byte on stack
    push    %rsp
    pop     %rsi                # buffer = stack
    push    $1
    pop     %rdx
    syscall
    pop     %rcx
    popq    %rdi                # restore free pointer
    ret

## ---- parse_tree -> eax (SF set on EOF) ----
parse_tree:
.Lp_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al           # '0'->0, '1'->1, '2'->2, whitespace->negative
    js      .Lp_read            # skip non-digit
    movl    %eax, %ecx          # ecx = child count (0,1,2); eax stays 0/1/2 so
                                # scasq leaves SF clear (caller's EOF test is js)
    movl    %edi, %edx          # edx = node base
    pushq   %rdx
    scasq                       # reserve two words (u, v): rdi += 8 in 2 bytes
    jrcxz   .Lp_done            # count 0 -> leaf: the reserved [0][0] node is it
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    parse_tree
    popq    %rdx
    popq    %rcx
    movl    %eax, (%rdx)        # store child
    addl    $4, %edx            # next slot
    loop    .Lp_loop
.Lp_done:
    popq    %rax                # return base address
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless via the heap-base threshold (rbx).
    cmpl    %ebx, (%rdx)               # CF = (u == 0)
    sbbl    %ecx, %ecx                 # ecx = -(u == 0)
    cmpl    %ebx, 4(%rdx)              # CF = (v == 0)
    sbbl    $-2, %ecx                  # ecx = tag in {0,1,2} = child count
    pushq   %rcx
    pushq   %rdx
    addb    $'0', %cl
    call    write_byte
    popq    %rdx
    popq    %rcx
    jrcxz   1f
.Lemit_loop:
    pushq   %rcx
    pushq   %rdx
    movl    (%rdx), %edx               # child = *slot (offset 0 then 4)
    call    *%rbp                      # emit_tree (rbp retargeted to it in _start)
    popq    %rdx
    popq    %rcx
    addl    $4, %edx                   # next slot
    loop    .Lemit_loop
1:  ret

## ---- apply(edx=a, esi=b) -> eax ----  (placed last; see leaf note below)
apply:
    movl    (%rdx), %eax               # eax = a.u
    movl    4(%rdx), %ecx              # ecx = a.v
    jrcxz   .La_build                  # a.v == 0 -> a is leaf or stem

    movl    (%rax), %ecx               # u.u
    jrcxz   .Lu_leaf
    movl    4(%rax), %ecx              # u.v
    jrcxz   .Lu_stem

    ## u = fork(w, x): triage on b.  b=leaf->w ; b=stem(z)->x·z ; b=fork(p,q)->y·p·q
    movl    (%rsi), %ecx               # b.u
    jrcxz   .Lb_leaf
    movl    4(%rsi), %ecx              # b.v  (== q, kept in ecx for .Lb_fork)
    jrcxz   .Lb_stem
.Lb_fork:
    pushq   %rcx                       # save q = b.v (already in ecx)
    movl    (%rsi), %esi               # p = b.u
    movl    4(%rdx), %edx              # y = a.v
    call    *%rbp                      # apply(y, p) -> eax
    popq    %rsi                       # esi = q
    xchg    %eax, %edx                 # edx = y·p
    jmp     *%rbp                      # tail apply(y·p, q)
.Lb_stem:
    movl    4(%rax), %edx              # x = u.v
    movl    (%rsi), %esi               # z = b.u
    jmp     *%rbp                      # tail apply(x, z)
.Lb_leaf:
    movl    (%rax), %eax               # w = u.u
    ret

.Lu_stem:
    ## rule 2: (x.b).(y.b) where u=stem(x), a=fork(u,y)
    pushq   %rdx
    pushq   %rsi
    movl    (%rax), %edx               # x = u.u
    call    *%rbp                      # apply(x, b) -> eax
    popq    %rsi
    popq    %rdx
    pushq   %rax                       # save x·b
    movl    4(%rdx), %edx              # y = a.v
    call    *%rbp                      # apply(y, b) -> eax
    xchg    %eax, %esi                 # esi = y·b
    popq    %rdx                       # edx = x·b
    jmp     *%rbp                      # tail apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].
    testl   %eax, %eax                 # a.u == 0 -> leaf
    jnz     1f
    xchg    %eax, %esi                 # leaf: eax=b, esi=0 (old a.u)
1:  pushq   %rdi
    stosl                              # write u = eax
    xchg    %eax, %esi                 # eax = v
    stosl                              # write v
    popq    %rax
    ret

## Canonical leaf at end of .text (first byte past the loaded image = [0][0]
## via BSS zero-fill). apply must stay within 127 bytes of leaf (disp8).
leaf:

.bss
.lcomm heap, 0x20000000
