# ============================================================
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
#   rdi = free pointer (permanent, absolute)
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
    leal    8(%rbx), %edi       # rdi = heap free pointer

    ## Build identity: fork(stem(leaf), stem(leaf)) — Jay identity
    movl    %edi, %ebp          # ebp = stem addr
    push    $1
    pop     %rax
    stosl                       # stem.tag = 1
    xchg    %ebx, %eax          # eax = leaf, ebx = 1 (temp)
    stosl                       # stem.child = leaf
    pushq   %rdi                # push fork addr (= result)
    xchg    %ebx, %eax          # eax = 1, ebx = leaf (restored)
    incl    %eax                # eax = 2
    stosl                       # fork.tag = 2
    xchg    %ebp, %eax          # eax = stem addr
    stosl                       # fork.left = stem(leaf)
    stosl                       # fork.right = stem(leaf)
    leaq    apply(%rip), %rbp   # rbp = &apply (call *%rbp = 2B vs 5B)

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    movb    $60, %al            # SYS_EXIT
    xorl    %edi, %edi
    syscall

## ---- apply(edx=a, esi=b) -> eax ----
apply:
    movl    (%rdx), %ecx
    cmpl    $2, %ecx
    jae     .La_fork

    ## a=leaf (ecx=0) or a=stem (ecx=1): build [tag+1, ...a.children, b]
    pushq   %rdi                       # save result addr
    leal    1(%rcx), %eax              # tag = a.tag + 1
    stosl                              # write tag
    jrcxz   1f                         # leaf: no children to copy
    movl    4(%rdx), %eax              # a.child (stem case)
    stosl                              # write it
1:  xchg    %esi, %eax                 # eax = b
    stosl                              # append b
    popq    %rax                       # result = start of node
    ret

.La_fork:
    movl    4(%rdx), %eax              # u (eax = u addr)
    movl    (%rax), %ecx               # u.tag
    jrcxz   .Lu_leaf
    decl    %ecx
    jz      .Lu_stem

    ## u=fork: rule 3 (Jay F) — b·w·x  (y is ignored)
    movl    %esi, %edx                 # edx = b
    movl    4(%rax), %esi              # esi = w = u.left (eax=u still)
    movl    8(%rax), %eax              # eax = x = u.right (u gone)
    pushq   %rax                       # save x
    call    *%rbp                      # apply(b, w) -> eax
    popq    %rsi                       # esi = x
    xchg    %eax, %edx                 # edx = b·w (1B)
    jmp     apply                      # tail call apply(b·w, x)

.Lu_stem:
    ## rule 2 (Jay S): y·b · (x·b) where u=stem(x), a=fork(u,y)
    pushq   %rdx                       # save a       [a]
    pushq   %rsi                       # save b       [b][a]
    movl    4(%rax), %edx              # x = u.child
    call    *%rbp                      # apply(x, b) -> eax
    popq    %rsi                       # restore b
    popq    %rdx                       # restore a
    pushq   %rax                       # save x·b     [x·b]
    movl    8(%rdx), %edx              # y = a.right
    call    *%rbp                      # apply(y, b) -> eax
    xchg    %eax, %edx                 # edx = y·b (1B)
    popq    %rsi                       # esi = x·b
    jmp     apply                      # tail call apply(y·b, x·b)

.Lu_leaf:
    ## rule 1: a.right
    movl    8(%rdx), %eax
    ret

## (alloc_fork/alloc_stem removed — unified into apply body + inlined _start)

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
## Read byte via do_io. If 0, return leaf. On EOF, return with SF set.
## Otherwise: stosl tag, pre-bump rdi, recurse d times storing children.
parse_tree:
.Lp_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al           # ZF if '0', SF if < '0'
    jz      .Lp_leaf             # leaf: return heap base
    js      .Lp_read            # skip non-digit
    movl    %edi, %edx
    stosl                       # store tag
    pushq   %rdx
    leaq    (%rdi,%rax,4), %rdi # pre-bump free pointer past children
    xchg    %eax, %ecx          # ecx = loop counter
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    parse_tree
    popq    %rdx
    popq    %rcx
    addl    $4, %edx
    movl    %eax, (%rdx)
    loop    .Lp_loop
    popq    %rax                # return base address
    ret
.Lp_leaf:
    movl    %ebx, %eax          # leaf = heap base
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
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