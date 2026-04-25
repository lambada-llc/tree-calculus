# ============================================================
# Like asm-x86-64, but apply() is non-recursive: it uses an
# explicit continuation stack instead of the call stack,
# matching the strategy in eager-ternary-vm.hpp.
#
# Node layout (i32-aligned):
#   leaf:  [0]             — 4 bytes  (pre-allocated at heap base, zero from BSS)
#   stem:  [1] [child]     — 8 bytes
#   fork:  [2] [left] [right] — 12 bytes
#
# Node pointers are absolute addresses (no base register needed for access).
# rbx = pointer to leaf node = heap base.
#
# Registers:
#   rbx = leaf address (= heap base, permanent)
#   rdi = free pointer / write head (permanent, absolute)
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
    leaq    heap(%rip), %rbx    # rbx = heap base = leaf address
    leaq    8(%rbx), %rdi

    ## Build identity: fork(fork(leaf, leaf), leaf) — inlined
    push    $2
    pop     %rax                # eax = 2
    movl    %edi, %ebp          # ebp = inner fork addr
    stosl                       # inner.tag = 2
    xchg    %ebx, %eax          # eax = leaf, ebx = 2 (temp)
    stosl                       # inner.left = leaf
    stosl                       # inner.right = leaf
    pushq   %rdi                # push outer fork addr (= result)
    xchg    %ebx, %eax          # eax = 2, ebx = leaf (restored)
    stosl                       # outer.tag = 2
    xchg    %ebp, %eax          # eax = inner fork addr
    stosl                       # outer.left = inner
    movl    %ebx, %eax
    stosl                       # outer.right = leaf

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi          # 1 byte instead of 2
    call    apply
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    movb    $60, %al            # SYS_EXIT (upper bytes 0 from write return)
    xorl    %edi, %edi
    syscall

## (alloc_fork/alloc_stem removed — unified into .Lreduce body + inlined _start)

## ---- apply(edx=a, esi=b) -> eax ----
##
## Non-recursive VM-style evaluator.  Instead of recursing, we manage
## an explicit continuation stack on the machine stack.  A sentinel
## tag of -1 is pushed at entry to mark the bottom.  Two frame types:
##
##   APPLY_TO(arg):       tag = 0   Pushed as: pushq arg; pushq $0
##   COMPUTE_AND_APPLY(fn, arg): tag = 1   Pushed as: pushq arg; pushq fn; pushq $1
##
## Sentinel:  pushq $-1 at entry.  Detected in dispatch because -1 is
## negative while valid tags (0, 1) are not.
##
## The loop alternates between two labels:
##   .Lreduce   — evaluate apply(a=edx, b=esi), producing a result in eax
##   .Ldispatch — consume the result through the continuation stack
##
apply:
    pushq   $-1                 # sentinel: marks bottom of continuation stack

.Lreduce:
    movl    (%rdx), %ecx
    cmpl    $2, %ecx
    jae     .Lvm_a_fork

    ## a=leaf (ecx=0) or a=stem (ecx=1): build [tag+1, ...a.children, b]
    pushq   %rdi                # save result addr
    leal    1(%rcx), %eax       # tag = a.tag + 1
    stosl                       # write tag
    jrcxz   1f                  # leaf: no children to copy
    movl    4(%rdx), %eax       # a.child (stem case)
    stosl                       # write it
1:  xchg    %esi, %eax          # eax = b
    stosl                       # append b
    popq    %rax                # result
    jmp     .Ldispatch

.Lvm_a_fork:
    movl    4(%rdx), %eax       # eax = u = a.left
    movl    (%rax), %ecx        # u.tag
    jrcxz   .Lvm_u_leaf
    decl    %ecx
    jz      .Lvm_u_stem

    ## ---- u = fork(w, x): triage on b ----
    movl    (%rsi), %ecx
    jrcxz   .Lvm_b_leaf
    decl    %ecx
    jz      .Lvm_b_stem

    ## b = fork(d, e):  apply(apply(y, d), e)
    ## Load e, y, d; then push APPLY_TO(e) and reduce apply(y, d)
    movl    8(%rsi), %eax       # eax = e = b.right
    movl    8(%rdx), %edx       # edx = y = a.right
    movl    4(%rsi), %esi       # esi = d = b.left
.Lpush_at_reduce:
    pushq   %rax                # push e (or result in CAA case)
    pushq   $0                  # tag = APPLY_TO
    jmp     .Lreduce

.Lvm_u_stem:
    ## u = stem(u'):  apply(apply(u', b), apply(y, b))
    ## Push COMPUTE_AND_APPLY(u', b)
    movl    4(%rax), %eax       # eax = u' = u.child
    pushq   %rsi                # arg2 = b
    pushq   %rax                # arg1 = u'
    pushq   $1                  # tag = COMPUTE_AND_APPLY
    ## Reduce apply(y, b)  (b unchanged)
    movl    8(%rdx), %edx       # a = y = a.right
    jmp     .Lreduce

.Lvm_b_stem:
    ## b = stem(d):  apply(x, d)
    movl    8(%rax), %edx       # a = x = u.right
    movl    4(%rsi), %esi       # b = d = b.child
    jmp     .Lreduce

    ## (Lvm_a_leaf/Lvm_a_stem removed — unified above)

.Lvm_u_leaf:
    ## apply(fork(leaf, y), b) = y
    movl    8(%rdx), %eax
    jmp     .Ldispatch

.Lvm_b_leaf:
    ## b = leaf:  result = w = u.left
    movl    4(%rax), %eax
    ## fall through to .Ldispatch

.Ldispatch:
    popq    %rcx                # frame tag: -1=sentinel, 0=APPLY_TO, 1=CAA
    jrcxz   .Lvm_at             # tag 0 -> APPLY_TO
    incl    %ecx
    jz      .Lvm_done           # -1+1=0 -> sentinel, done

    ## COMPUTE_AND_APPLY(fn, arg):
    ##   Push APPLY_TO(result), then reduce apply(fn, arg)
    popq    %rdx                # fn -> a
    popq    %rsi                # arg -> b
    jmp     .Lpush_at_reduce    # push result + AT tag, then reduce

.Lvm_at:
    ## APPLY_TO: a = result, b = arg
    popq    %rsi                # b = arg
    xchg    %eax, %edx          # a = result (1 byte vs 2 for movl)
    jmp     .Lreduce

.Lvm_done:
    ret

## ---- I/O: shared syscall (saves/restores rdi=free ptr) ----
## Uses the stack as a single-byte I/O buffer (à la Justine Tunney's blc.S).
## Callers pass the byte in %cl (for writes) and receive it in %cl (for reads).
write_byte:
    push    $1
    pop     %rax                # rax=1=SYS_WRITE
do_io:
    pushq   %rdi                # save free pointer
    movl    %eax, %edi          # fd = eax (0=stdin, 1=stdout)
    push    %rcx                # byte on stack (write: cl=data; read: overwritten)
    mov     %rsp, %rsi          # buffer = stack
    push    $1
    pop     %rdx
    syscall
    pop     %rcx                # read result in cl / clean up write
    popq    %rdi                # restore free pointer
    ret

## ---- parse_tree -> eax ----
parse_tree:
    pushq   %rbp
.Lp_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al            # byte from do_io (stack buffer) into al
    subb    $'0', %al           # ZF if '0', SF if < '0'
    jz      .Lp_leaf            # leaf: return heap base
    js      .Lp_read            # skip non-digit
    movl    %edi, %ebp
    stosl                       # store tag=d, rdi += 4 (eax preserved)
    pushq   %rbp                # save base
    leaq    (%rdi,%rax,4), %rdi # pre-bump free pointer past children
    xchg    %eax, %ecx          # ecx = loop counter (1 byte vs 2)
.Lp_loop:
    pushq   %rcx                # save counter across recursive call
    call    parse_tree          # eax = child offset
    popq    %rcx                # restore counter
    addl    $4, %ebp            # advance to next child slot
    movl    %eax, 0(%rbp)       # store child
    loop    .Lp_loop            # dec ecx; jnz
    popq    %rax                # return base address
    jmp     .Lp_ret
.Lp_leaf:
    movl    %ebx, %eax          # leaf = heap base
.Lp_ret:
    popq    %rbp
    ret

## ---- emit_tree(edx=tree) ----
emit_tree:
    movl    (%rdx), %ecx
    pushq   %rcx
    pushq   %rdx
    addb    $'0', %cl
    call    write_byte
    popq    %rdx
    popq    %rcx
    jrcxz   1f
.Lemit_loop:
    addl    $4, %edx
    pushq   %rcx
    pushq   %rdx
    movl    (%rdx), %edx
    call    emit_tree
    popq    %rdx
    popq    %rcx
    loop    .Lemit_loop
1:  ret

.bss
.lcomm heap, 0x20000000
