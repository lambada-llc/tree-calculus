# ============================================================
# Tree Calculus Evaluator — x86-64 Assembly (minbin I/O)
#
# Key insight: in minimalist binary encoding, parsing IS evaluation.
# Input:  1 = leaf (△), 0 A B = apply(A, B).
# A single recursive parse_eval() function replaces the identity
# bootstrap, main apply-loop, and parse_tree from the ternary variant.
#
# Internal heap is a BSS-allocated 64 MB region:
#   leaf:  [0]                — 4 bytes  (pre-allocated at offset 0, zero from BSS)
#   stem:  [1] [child]        — 8 bytes
#   fork:  [2] [left] [right] — 12 bytes
# Node pointers are absolute addresses (no base register needed for access).
# rbx = pointer to leaf node = heap base.
# Free pointer starts at heap + 8.
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

# ==== Entry point ====
# Set up heap from BSS, parse+eval the entire stdin expression, emit result, exit.
# No identity bootstrap. No apply-loop.
_start:
    leaq    heap(%rip), %rbx    # rbx = heap base = leaf address
    leaq    8(%rbx), %rdi       # rdi = free pointer, skip leaf@0
    leaq    apply(%rip), %rbp   # rbp = &apply (call *%rbp = 2B vs 5B)

    call    parse_eval          # parse + eval entire stdin → eax
    xchg    %eax, %edx          # edx = result (1 byte vs 2)
    call    emit_tree           # emit result in minbin

    movb    $SYS_EXIT, %al
    xorl    %edi, %edi
    syscall

# ==== parse_eval -> eax (tree offset) ====
#
# Reads one bit from stdin (as ASCII '0' or '1', skipping non-01 bytes).
#   '1' → return leaf (offset 0)
#   '0' → a = parse_eval(); b = parse_eval(); return apply(a, b)
#   EOF → return leaf as fallback
#
# This single function replaces parse_tree + identity bootstrap + main loop.
parse_eval:
.Lpe_read:
    xorl    %eax, %eax          # eax=0=SYS_READ, fd=stdin
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof/error
    jnz     .Lpe_leaf           # EOF: return leaf
    subb    $'0', %cl           # cl = char - '0'; CF if < '0'
    jb      .Lpe_read           # < '0': skip whitespace
    je      .Lpe_apply          # '0': application
    decb    %cl                 # was '1'? (cl → 0)
    jnz     .Lpe_read           # > '1': skip
                                # fallthrough: '1' → leaf
.Lpe_leaf:
    movl    %ebx, %eax          # leaf = heap base
    ret

.Lpe_apply:
    call    parse_eval          # a = parse first subexpr
    pushq   %rax                # save a
    call    parse_eval          # b = parse second subexpr
    xchg    %eax, %esi          # esi = b
    popq    %rdx                # edx = a
    jmp     apply               # tail call apply(a, b) → eax

# ==== apply(edx=a, esi=b) -> eax ====
# Triage calculus rules, identical to ../asm-x86-64/.
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
    movl    4(%rdx), %eax              # eax = u
    movl    (%rax), %ecx               # u.tag
    jrcxz   .Lu_leaf
    decl    %ecx
    jz      .Lu_stem

    ## u=fork: triage dispatch — w,x contiguous in u; y in a
    movl    (%rsi), %ecx               # ecx = b.tag (0, 1, or 2)
    movl    8(%rdx), %edx              # speculatively load y = a.right
    cmpl    $2, %ecx
    jae     1f                         # b.tag=2 → keep y
    movl    4(%rax,%rcx,4), %edx       # b.tag=0→w=u.left, b.tag=1→x=u.right
1:
    leal    4(%rsi), %esi              # rsi -> b.child[0]
    jrcxz   .Ltriage_done
.Ltriage_loop:
    pushq   %rcx
    lodsl                              # eax = [rsi], rsi += 4
    pushq   %rsi                       # save next-child ptr
    xchg    %eax, %esi                 # esi = child
    call    *%rbp                      # apply(edx, esi) -> eax
    xchg    %eax, %edx                 # edx = new result (1B)
    popq    %rsi                       # restore pointer
    popq    %rcx
    loop    .Ltriage_loop
.Ltriage_done:
    xchg    %edx, %eax                 # return in eax
    ret

.Lu_stem:
    ## rule 2: apply(apply(x, b), apply(y, b))
    pushq   %rdx                       # save a
    pushq   %rsi                       # save b
    movl    4(%rax), %edx              # x = u.child
    call    *%rbp                      # apply(x, b)
    popq    %rsi                       # restore b
    popq    %rdx                       # restore a
    pushq   %rax                       # save x·b
    movl    8(%rdx), %edx              # y = a.right
    call    *%rbp                      # apply(y, b)
    xchg    %eax, %esi                 # esi = y·b
    popq    %rdx                       # edx = x·b
    jmp     apply                      # tail call

.Lu_leaf:
    ## rule 1: y
    movl    8(%rdx), %eax
    ret

## (alloc_fork/alloc_stem removed — unified into apply body)

# ==== I/O ====
# Uses the stack as a single-byte I/O buffer (à la Justine Tunney's blc.S).
# Callers pass the byte in %cl (for writes) and receive it in %cl (for reads).
write_byte:
    push    $1
    pop     %rax                # rax=1=SYS_WRITE (must clear upper bytes!)
do_io:
    pushq   %rdi                # save free pointer
    movl    %eax, %edi          # fd
    push    %rcx                # byte on stack (write: cl=data; read: overwritten)
    mov     %rsp, %rsi          # buffer = stack
    push    $1
    pop     %rdx                # count = 1
    syscall
    pop     %rcx                # read result in cl / clean up write
    popq    %rdi                # restore free pointer
    ret

# ==== emit_tree(edx=tree) → minbin on stdout ====
#
# Minbin value encoding:
#   leaf         → '1'
#   stem(child)  → '0' '1' <child>
#   fork(l, r)   → '0' '0' '1' <left> <right>
#
# i.e., emit (tag) zeros, then '1', then recurse on children.
emit_tree:
    movl    (%rdx), %ecx              # ecx = tag (0, 1, or 2)
    pushq   %rdx                       # save node for function lifetime

    ## Emit ecx zeros
    jrcxz   .Le_one
    pushq   %rcx
.Le_zero_loop:
    pushq   %rcx
    movb    $'0', %cl
    call    write_byte
    popq    %rcx
    loop    .Le_zero_loop
    popq    %rcx

.Le_one:
    ## Emit '1'
    pushq   %rcx
    movb    $'1', %cl
    call    write_byte
    popq    %rcx

    ## Recurse on children
    jrcxz   .Le_done                   # leaf: no children
    popq    %rdx                       # restore node from entry
    decl    %ecx
    pushq   %rcx                       # save (tag-1)
    pushq   %rdx                       # save node for right child
    movl    4(%rdx), %edx              # first child
    call    emit_tree
    popq    %rdx                       # restore node
    popq    %rcx
    jrcxz   1f                         # stem: done after one child
    movl    8(%rdx), %edx              # fork right child
    jmp     emit_tree                  # tail call

.Le_done:
    popq    %rdx                       # clean up entry push
1:  ret

.bss
.lcomm heap, 0x4000000
