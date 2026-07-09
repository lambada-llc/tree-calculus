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
# The cost is reconstructing the ternary tag (0/1/2) when we need it
# for emit and for triage dispatch, done branchlessly as
#   tag = 2 - (u==0) - (v==0).
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
    leaq    heap(%rip), %rbx    # rbx = heap base = leaf address ([0][0] from BSS)
    leal    8(%rbx), %edi       # rdi = heap free pointer (past the leaf node)

    ## Build identity: fork(fork(leaf, leaf), leaf) — inlined.
    ## In the two-word layout a fork is just [left][right].
    movl    %ebx, %eax          # eax = leaf
    movl    %edi, %ebp          # ebp = inner fork addr
    stosl                       # inner.u = leaf
    stosl                       # inner.v = leaf
    pushq   %rdi                # push outer fork addr (= result)
    xchg    %ebp, %eax          # eax = inner fork addr, ebp = leaf
    stosl                       # outer.u = inner
    xchg    %ebp, %eax          # eax = leaf, ebp = inner
    stosl                       # outer.v = leaf
    leaq    apply(%rip), %rbp   # rbp = &apply

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    movb    $60, %al            # SYS_EXIT
    xorl    %edi, %edi
    syscall

## ---- apply(edx=a, esi=b) -> eax ----
apply:
    movl    (%rdx), %eax               # eax = a.u
    movl    4(%rdx), %ecx              # ecx = a.v
    testl   %eax, %eax
    jnz     1f
    xchg    %eax, %esi                 # a=leaf: eax=b, esi=0 (old a.u)
1:  testl   %ecx, %ecx
    jnz     .La_fork                   # a.v != 0 -> a is a fork
    ## a=leaf -> build stem(b)=[b][0]; a=stem(x) -> build fork(x,b)=[x][b].
    ## Either way the node is [eax][esi].
    pushq   %rdi                       # save result addr
    stosl                              # write u = eax
    xchg    %eax, %esi                 # eax = v
    stosl                              # write v
    popq    %rax                       # result = start of node
    ret

.La_fork:
    ## a = fork(u, y): u = a.u (eax), y = a.v.
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
    cmpl    $0, 4(%rsi)                # b.v
    jz      .Lb_stem
.Lb_fork:
    movl    4(%rsi), %eax              # q = b.v
    pushq   %rax                       # save q
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
    subb    $'0', %al           # ZF if '0', SF if < '0'
    jz      .Lp_leaf            # leaf: return heap base
    js      .Lp_read            # skip non-digit
    xchg    %eax, %ecx          # ecx = child count (1 or 2)
    movl    %edi, %edx          # edx = node base
    pushq   %rdx
    addl    $8, %edi            # reserve two words (u, v)
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    parse_tree
    popq    %rdx
    popq    %rcx
    movl    %eax, (%rdx)        # store child
    addl    $4, %edx            # next slot
    loop    .Lp_loop
    popq    %rax                # return base address
    ret
.Lp_leaf:
    movl    %ebx, %eax          # leaf = heap base
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless.
    push    $2
    pop     %rcx
    cmpl    $1, (%rdx)                 # CF = (u == 0)
    sbbl    $0, %ecx
    cmpl    $1, 4(%rdx)                # CF = (v == 0)
    sbbl    $0, %ecx                   # ecx = tag in {0,1,2} = child count
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

.bss
.lcomm heap, 0x20000000
