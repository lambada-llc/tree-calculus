# ============================================================
# Tagless two-word node representation (see x64.s), with minbin I/O:
# parsing IS evaluation.  Input:  1 = leaf, 0 A B = apply(A, B).
# A single recursive parse_eval() replaces the identity bootstrap,
# apply-loop, and ternary parse_tree.
#
#   Every node is exactly two i32 words, [u][v] (8 bytes).
#     u == 0            -> leaf   (v ignored; the canonical leaf is [0][0])
#     u != 0, v == 0    -> stem(u)
#     u != 0, v != 0    -> fork(u, v)
#
# Registers:
#   rbx = leaf address (= heap base, permanent)
#   rdi = free pointer (permanent, absolute)
#   rbp = &apply while parsing/reducing, retargeted to &emit_tree for output
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
    leaq    apply(%rip), %rbp   # rbp = &apply
    .byte   0x8d, 0x5d          # leal disp8(%rbp), %ebx  (ModRM 5d: reg=ebx, base=rbp)
    .byte   leaf-apply          # rbx = leaf = heap base (disp8 = sizeof(apply))
    leal    8(%rbx), %edi       # rdi = free pointer, skip leaf node

    call    parse_eval          # parse + eval entire stdin -> eax
    xchg    %eax, %edx          # edx = result (1 byte vs 2)
    ## parse/eval done; retarget the dead rbp (= &apply) to emit_tree.
    .byte   0x8d, 0x6d          # leal disp8(%rbp), %ebp  (ModRM 6d: reg=ebp, base=rbp)
    .byte   emit_tree-apply     # disp8 = emit_tree - apply
    call    *%rbp               # emit_tree(edx = result)
    movb    $SYS_EXIT, %al
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

## ---- parse_eval -> eax (tree pointer) ----
## '1' -> leaf ; '0' -> apply(parse_eval, parse_eval) ; EOF -> leaf.
parse_eval:
.Lpe_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lpe_leaf           # EOF: return leaf
    subb    $'0', %cl           # cl = char - '0'; CF if < '0'
    jb      .Lpe_read           # < '0': skip whitespace
    je      .Lpe_apply          # '0': application
    decb    %cl                 # was '1'?
    jnz     .Lpe_read           # > '1': skip
.Lpe_leaf:
    movl    %ebx, %eax          # leaf = heap base
    ret
.Lpe_apply:
    call    parse_eval          # a = first subexpr
    pushq   %rax
    call    parse_eval          # b = second subexpr
    xchg    %eax, %esi          # esi = b
    popq    %rdx                # edx = a
    jmp     *%rbp               # tail apply(a, b)   (rbp = &apply here)

## ---- emit_tree(edx=tree) -> minbin on stdout ----
## leaf -> '1' ; stem(c) -> '0' '1' <c> ; fork(l,r) -> '0' '0' '1' <l> <r>.
## i.e. emit (tag) zeros, then '1', then recurse on children.
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless via the heap-base threshold (rbx).
    cmpl    %ebx, (%rdx)
    sbbl    %ecx, %ecx
    cmpl    %ebx, 4(%rdx)
    sbbl    $-2, %ecx                  # ecx = tag in {0,1,2}
    pushq   %rdx                       # save node for the function lifetime
    jrcxz   .Le_one                    # tag 0 (leaf): no leading zeros
    pushq   %rcx
.Le_zero_loop:
    pushq   %rcx
    movb    $'0', %cl
    call    write_byte
    popq    %rcx
    loop    .Le_zero_loop
    popq    %rcx
.Le_one:
    pushq   %rcx
    movb    $'1', %cl
    call    write_byte
    popq    %rcx
    jrcxz   .Le_done                   # leaf: no children
    popq    %rdx                       # restore node
    decl    %ecx                       # tag-1
    pushq   %rcx
    pushq   %rdx
    movl    (%rdx), %edx               # first child (offset 0)
    call    *%rbp                      # emit_tree (rbp retargeted to it)
    popq    %rdx
    popq    %rcx
    jrcxz   1f                         # stem: done after one child
    movl    4(%rdx), %edx              # fork right child (offset 4)
    jmp     *%rbp                      # tail emit_tree
.Le_done:
    popq    %rdx                       # clean up entry push
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
    pushq   %rcx                       # save q = b.v
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
    call    *%rbp                      # apply(x, b)
    popq    %rsi
    popq    %rdx
    pushq   %rax                       # save x·b
    movl    4(%rdx), %edx              # y = a.v
    call    *%rbp                      # apply(y, b)
    xchg    %eax, %esi                 # esi = y·b
    popq    %rdx                       # edx = x·b
    jmp     *%rbp                      # tail apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].
    testl   %eax, %eax
    jnz     1f
    xchg    %eax, %esi                 # leaf: eax=b, esi=0
1:  pushq   %rdi
    stosl                              # write u
    xchg    %eax, %esi
    stosl                              # write v
    popq    %rax
    ret

## Canonical leaf at end of .text ([0][0] via BSS zero-fill). apply must
## stay within 127 bytes of leaf (disp8 for leaf-apply / emit_tree-apply).
leaf:

.bss
.lcomm heap, 0x20000000
