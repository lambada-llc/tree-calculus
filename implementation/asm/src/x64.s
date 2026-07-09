# ============================================================
# Reimplemented using the eager-ternary-ref strategy, but with a
# tagless "two-word" node representation:
#
#   Every node is exactly two i32 words, [u][v] (8 bytes).
#     u == 0            -> leaf   (v ignored; the canonical leaf is [0][0])
#     u != 0, v == 0    -> stem(u)
#     u != 0, v != 0    -> fork(u, v)
#
# There is no tag word: leaf/stem/fork are implicit in whether the
# child pointers are null. Child pointers are absolute heap addresses,
# which are always non-zero, so 0 unambiguously means "no child".
#
# This packs forks into 8 bytes (vs 12 for the tagged [tag][l][r]
# layout) and makes construction a pair of stores with no tag write.
# apply's leaf/stem/fork split and the triage dispatch fall out of
# null-checks (jrcxz) with jmp *%rbp tail calls; the ternary tag
# (0/1/2) is reconstructed branchlessly (2 - (u==0) - (v==0)) only
# where genuinely needed — in emit.
#
# Node pointers are absolute addresses (no base register needed for access).
# rbx = pointer to the canonical leaf node = heap base.
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
    ## rbp = &apply, loaded first so the leaf/heap base can be derived
    ## from it. `apply` is the last function, so `leaf` sits just past it
    ## and `leaf-apply` is a small intra-.text displacement (disp8) — this
    ## keeps everything PC-relative (no absolute relocations) yet compact.
    leaq    apply(%rip), %rbp   # rbp = &apply
    ## rbx = leaf address = heap base ([0][0] from BSS) = apply + sizeof(apply).
    ## Hand-encoded `leal (leaf-apply)(%rbp), %ebx`: leaf-apply is a forward
    ## reference so the assembler would pick the 6-byte disp32 form, but it
    ## fits in a signed byte (guarded at end of file), so force disp8 = 3B.
    .byte   0x8d, 0x5d          # leal disp8(%rbp), %ebx  (ModRM 5d: mod=01, reg=ebx, base=rbp)
    .byte   leaf-apply          # disp8 = sizeof(apply)
    leal    8(%rbx), %edi       # rdi = heap free pointer (past the leaf node)

    ## Build identity: fork(fork(leaf, leaf), leaf) — inlined.
    ## In the two-word layout a fork is just [left][right]. Uses esi (not
    ## ebp) as scratch so rbp keeps pointing at apply — no reload needed.
    movl    %ebx, %eax          # eax = leaf
    movl    %edi, %esi          # esi = inner fork addr
    stosl                       # inner.u = leaf
    stosl                       # inner.v = leaf
    pushq   %rdi                # push outer fork addr (= result)
    xchg    %esi, %eax          # eax = inner fork addr, esi = leaf
    stosl                       # outer.u = inner
    xchg    %esi, %eax          # eax = leaf, esi = inner
    stosl                       # outer.v = leaf

1:  call    parse_tree
    popq    %rdx                # accumulator (pop before the EOF test; pop leaves flags)
    js      2f
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  call    emit_tree
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
## Read byte via do_io. '0' -> leaf. On EOF, return with SF set.
## '1'/'2': allocate a two-word node, recurse to fill 1 or 2 children;
## unfilled trailing word stays 0 (BSS), which is exactly the stem's v.
parse_tree:
.Lp_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al           # '0'->0, '1'->1, '2'->2, whitespace->negative
    js      .Lp_read            # skip non-digit
    movl    %eax, %ecx          # ecx = child count (0, 1 or 2); eax stays 0/1/2 so
                                # scasq below leaves SF clear (caller's EOF test is `js`)
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
    ## tag = 2 - (u==0) - (v==0), branchless. Every non-null child pointer
    ## is >= the heap base (rbx), so `cmpl %ebx, word` sets CF iff word==0.
    ##   sbb %ecx,%ecx  -> ecx = -CF = -(u==0)          {0 or -1}
    ##   sbb $-2,%ecx   -> ecx = ecx + 2 - CF = tag     {leaf 0, stem 1, fork 2}
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
    call    emit_tree
    popq    %rdx
    popq    %rcx
    addl    $4, %edx                   # next slot
    loop    .Lemit_loop
1:  ret

## ---- apply(edx=a, esi=b) -> eax ----
## Placed last so `leaf` (the label just below) is a short intra-.text
## displacement from `apply`, letting _start derive the heap base from rbp.
apply:
    movl    (%rdx), %eax               # eax = a.u
    movl    4(%rdx), %ecx              # ecx = a.v
    jrcxz   .La_build                  # a.v == 0 -> a is leaf or stem

    ## a = fork(u, y): u = a.u (eax), y = a.v.
    ## (any node with a.v != 0 has a.u != 0, so eax is a valid pointer.)
    movl    (%rax), %ecx               # u.u
    jrcxz   .Lu_leaf
    movl    4(%rax), %ecx              # u.v
    jrcxz   .Lu_stem

    ## u = fork(w, x): triage on b. w=u.u, x=u.v; y=a.v.
    ## Dispatch directly on b's shape — no tag arithmetic needed:
    ##   b=leaf     -> w
    ##   b=stem(z)  -> x·z
    ##   b=fork(p,q)-> y·p·q
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
    pushq   %rdx                       # save a       [a]
    pushq   %rsi                       # save b       [b][a]
    movl    (%rax), %edx               # x = u.u
    call    *%rbp                      # apply(x, b) -> eax
    popq    %rsi                       # restore b
    popq    %rdx                       # restore a
    pushq   %rax                       # save x·b     [x·b]
    movl    4(%rdx), %edx              # y = a.v
    call    *%rbp                      # apply(y, b) -> eax
    xchg    %eax, %esi                 # esi = y·b (1B)
    popq    %rdx                       # edx = x·b
    jmp     *%rbp                      # tail apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> build stem(b)=[b][0]; a=stem(x) -> build fork(x,b)=[x][b].
    testl   %eax, %eax                 # a.u == 0 -> leaf
    jnz     1f
    xchg    %eax, %esi                 # leaf: eax=b, esi=0 (old a.u)
1:  pushq   %rdi                       # save result addr
    stosl                              # write u = eax
    xchg    %eax, %esi                 # eax = v
    stosl                              # write v
    popq    %rax                       # result = start of node
    ret

## The canonical leaf lives here at the end of .text: its address is the
## first byte past the loaded image, so [leaf] = [0][0] via BSS zero-fill,
## and the bump allocator grows upward from leaf+8. The .lcomm below only
## exists to enlarge p_memsz so the kernel maps a big zero heap.
leaf:
## NOTE: _start hand-encodes (leaf-apply) as a disp8, so `apply` must stay
## within 127 bytes of `leaf` (it is currently ~83). If apply grows past
## that, the leaf pointer silently wraps — keep apply the last function.

.bss
.lcomm heap, 0x20000000
