# ============================================================
# Tagless two-word node representation (see x64.s), but apply() is
# non-recursive: it uses an explicit continuation stack on the machine
# stack instead of the call stack (cf. eager-ternary-vm.hpp).
#
#   Every node is exactly two i32 words, [u][v] (8 bytes).
#     u == 0            -> leaf   (v ignored; the canonical leaf is [0][0])
#     u != 0, v == 0    -> stem(u)
#     u != 0, v != 0    -> fork(u, v)
#
# Registers:
#   rbx = leaf address (= heap base, permanent)
#   rdi = free pointer (permanent, absolute)
#   rbp = &apply (call *%rbp in the fold loop)
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
    leal    8(%rbx), %edi       # rdi = free pointer, past the leaf node
    leaq    apply(%rip), %rbp   # rbp = &apply

    ## Build identity: fork(fork(leaf, leaf), leaf) — two-word forks.
    movl    %ebx, %eax          # eax = leaf
    movl    %edi, %esi          # esi = inner fork addr
    stosl                       # inner.u = leaf
    stosl                       # inner.v = leaf
    pushq   %rdi                # push outer fork addr (= result)
    xchg    %esi, %eax          # eax = inner
    stosl                       # outer.u = inner
    xchg    %esi, %eax          # eax = leaf
    stosl                       # outer.v = leaf

1:  call    parse_tree
    popq    %rdx                # accumulator (pop before EOF test; pop leaves flags)
    js      2f
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  call    emit_tree
    movb    $60, %al            # SYS_EXIT
    xorl    %edi, %edi
    syscall

## ---- apply(edx=a, esi=b) -> eax ----
## Non-recursive VM: an explicit continuation stack on the machine stack.
## Frame tags:  -1 = sentinel (bottom), 0 = APPLY_TO(arg), 1 = COMPUTE_AND_APPLY(fn,arg).
## These frame tags are on the machine stack and are unrelated to the
## tagless node layout below.
apply:
    pushq   $-1                 # sentinel: marks bottom of continuation stack
.Lreduce:
    movl    (%rdx), %eax        # a.u
    movl    4(%rdx), %ecx       # a.v
    jrcxz   .Lvm_a_build        # a.v == 0 -> a is leaf or stem

    ## a = fork(u, y): u = a.u (eax). Classify u.
    movl    (%rax), %ecx        # u.u
    jrcxz   .Lvm_u_leaf
    movl    4(%rax), %ecx       # u.v
    jrcxz   .Lvm_u_stem

    ## u = fork(w, x): triage on b.
    movl    (%rsi), %ecx        # b.u
    jrcxz   .Lvm_b_leaf
    movl    4(%rsi), %ecx       # b.v
    jrcxz   .Lvm_b_stem

    ## b = fork(d, e): apply(apply(y, d), e).
    ## Load e, y, d; push APPLY_TO(e) and reduce apply(y, d).
    movl    4(%rsi), %eax       # e = b.v
    movl    4(%rdx), %edx       # y = a.v
    movl    (%rsi), %esi        # d = b.u
.Lpush_at_reduce:
    pushq   %rax                # push e (or result in CAA case)
    pushq   $0                  # tag = APPLY_TO
    jmp     .Lreduce

.Lvm_u_stem:
    ## u = stem(u'): apply(apply(u', b), apply(y, b)).
    ## Push COMPUTE_AND_APPLY(u', b), reduce apply(y, b).
    movl    (%rax), %eax        # u' = u.u
    pushq   %rsi                # arg2 = b
    pushq   %rax                # arg1 = u'
    pushq   $1                  # tag = COMPUTE_AND_APPLY
    movl    4(%rdx), %edx       # a = y = a.v
    jmp     .Lreduce

.Lvm_b_stem:
    ## b = stem(d): apply(x, d).  x = u.v, d = b.u
    movl    4(%rax), %edx       # a = x = u.v
    movl    (%rsi), %esi        # b = d = b.u
    jmp     .Lreduce

.Lvm_u_leaf:
    ## apply(fork(leaf, y), b) = y = a.v
    movl    4(%rdx), %eax
    jmp     .Ldispatch

.Lvm_b_leaf:
    ## b = leaf: result = w = u.u
    movl    (%rax), %eax
    ## fall through to .Ldispatch

.Ldispatch:
    popq    %rcx                # frame tag: -1=sentinel, 0=APPLY_TO, 1=CAA
    jrcxz   .Lvm_at             # 0 -> APPLY_TO
    incl    %ecx
    jz      .Lvm_done           # -1 -> sentinel, done

    ## COMPUTE_AND_APPLY(fn, arg): push APPLY_TO(result), reduce apply(fn, arg)
    popq    %rdx                # fn -> a
    popq    %rsi                # arg -> b
    jmp     .Lpush_at_reduce

.Lvm_at:
    ## APPLY_TO: a = result, b = arg
    popq    %rsi                # b = arg
    xchg    %eax, %edx          # a = result
    jmp     .Lreduce

.Lvm_done:
    ret

.Lvm_a_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].  Then dispatch.
    testl   %eax, %eax          # a.u == 0 -> leaf
    jnz     1f
    xchg    %eax, %esi          # leaf: eax=b, esi=0
1:  pushq   %rdi
    stosl                       # write u
    xchg    %eax, %esi
    stosl                       # write v
    popq    %rax                # result
    jmp     .Ldispatch

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
    scasq                       # reserve two words (u, v)
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
    cmpl    %ebx, (%rdx)
    sbbl    %ecx, %ecx
    cmpl    %ebx, 4(%rdx)
    sbbl    $-2, %ecx
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
    movl    (%rdx), %edx        # child = *slot (offset 0 then 4)
    call    emit_tree
    popq    %rdx
    popq    %rcx
    addl    $4, %edx            # next slot
    loop    .Lemit_loop
1:  ret

.bss
.lcomm heap, 0x20000000
