# ============================================================
# Variant: stores application trees in memory rather than ternary.
# Input:  1 = leaf (△), 0 A B = apply(A, B).
# A single recursive parse_eval() function replaces the identity
# bootstrap, main apply-loop, and parse_tree from the ternary variant.
#
# Memory representation: application trees rather than ternary nodes.
# Only two node types in the heap:
#   leaf:  [0]                — 4 bytes  (pre-allocated at offset 0, zero from BSS)
#   app:   [1] [left] [right] — 12 bytes
# Ternary forms are encoded as nested apps:
#   stem(x)    = App(leaf, x)
#   fork(x, y) = App(App(leaf, x), y)
# Pattern matching in apply() looks deeper into the tree structure,
# but allocation and emission are simplified.
#
# Emission maps directly: leaf → '1', App(a,b) → '0' + emit(a) + emit(b).
# The minbin encoding is isomorphic to the deep app-tree representation.
#
# Node pointers are absolute addresses (no base register needed for access).
# rbx = pointer to leaf node = heap base.
# Free pointer starts at heap + 4 (past the 4-byte leaf node).
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
    leaq    4(%rbx), %rdi       # rdi = free pointer, skip 4-byte leaf@0
    leaq    apply(%rip), %rbp   # rbp = &apply (call *%rbp = 2B vs 5B)

    call    parse_eval          # parse + eval entire stdin → eax
    xchg    %eax, %edx          # edx = result (1 byte vs 2)
    call    emit_tree           # emit result in minbin

    movb    $SYS_EXIT, %al
    xorl    %edi, %edi
    syscall

# ==== parse_eval -> eax (tree pointer) ====
#
# Reads one bit from stdin (as ASCII '0' or '1', skipping non-01 bytes).
#   '1' → return leaf
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
#
# Deep app-tree pattern matching.  Ternary forms are recognized by
# structure rather than tags:
#   leaf       = tag 0
#   stem(x)    = App(leaf, x)          — tag 1, left.tag 0
#   fork(u, y) = App(App(leaf, u), y)  — tag 1, left.tag 1
#
# The seven reduction rules:
#   apply(leaf, b)                        = App(leaf, b)                    [stem ctor]
#   apply(App(leaf,x), b)                 = App(App(leaf,x), b)            [fork ctor]
#   apply(App(App(leaf,leaf),y), b)       = y                              [rule 1]
#   apply(App(App(leaf,stem(x)),y), b)    = apply(apply(x,b), apply(y,b))  [rule 2]
#   apply(App(App(leaf,fork(w,x)),y), leaf)       = w                      [rule 3a]
#   apply(App(App(leaf,fork(w,x)),y), stem(d))    = apply(x, d)            [rule 3b]
#   apply(App(App(leaf,fork(w,x)),y), fork(c,d))  = apply(apply(y,c), d)   [rule 3c]
apply:
    movl    (%rdx), %ecx
    jrcxz   .La_leaf                           # a = leaf (tag 0)

    ## a is App(a.left, a.right) — check a.left to distinguish stem vs fork
    movl    4(%rdx), %eax                      # eax = a.left
    movl    (%rax), %ecx                       # a.left.tag
    jrcxz   .La_stem                           # a.left = leaf → a is stem-like

    ## a is fork-like: a = App(App(leaf, u), y)
    ## a.left = App(leaf, u), so u = a.left.right
    movl    8(%rax), %eax                      # eax = u = a.left.right
    movl    (%rax), %ecx                       # u.tag
    jrcxz   .Lu_leaf                           # u = leaf → rule 1

    ## u is App — check u.left for stem-like vs fork-like
    movl    4(%rax), %ecx                      # ecx = u.left (pointer)
    movl    (%rcx), %ecx                       # ecx = u.left.tag
    jrcxz   .Lu_stem                           # u.left = leaf → u = stem → rule 2

    ## u = fork(w, x): w = u.left.right, x = u.right. Triage on b.
    movl    (%rsi), %ecx
    jrcxz   .Lb_leaf                           # b = leaf → rule 3a

    ## b is App — check b.left for stem-like vs fork-like
    movl    4(%rsi), %ecx                      # ecx = b.left (pointer)
    movl    (%rcx), %ecx                       # ecx = b.left.tag
    jrcxz   .Lb_stem                           # b.left = leaf → b = stem → rule 3b

    ## 3c: b = fork(c, d). apply(apply(y, c), d)
    ## c = b.left.right, d = b.right
    pushq   %rsi                               # save b
    movl    8(%rdx), %edx                      # y = a.right
    movl    4(%rsi), %esi                      # esi = b.left (stem-of-c node)
    movl    8(%rsi), %esi                      # esi = b.left.right = c
    call    *%rbp                              # apply(y, c)
    popq    %rsi                               # restore b
    xchg    %eax, %edx                         # edx = result
    movl    8(%rsi), %esi                      # d = b.right
    jmp     apply                              # tail call

.Lu_stem:
    ## rule 2: u = stem(x). apply(apply(x, b), apply(y, b))
    ## x = u.right = [8(%rax)], y = a.right = [8(%rdx)]
    pushq   %rdx                               # save a
    pushq   %rsi                               # save b
    movl    8(%rax), %edx                      # x = u.right
    call    *%rbp                              # apply(x, b)
    popq    %rsi                               # restore b
    popq    %rdx                               # restore a
    pushq   %rax                               # save x·b
    movl    8(%rdx), %edx                      # y = a.right
    call    *%rbp                              # apply(y, b)
    xchg    %eax, %esi                         # esi = y·b
    popq    %rdx                               # edx = x·b
    jmp     apply                              # tail call

.Lb_stem:
    ## 3b: b = stem(d). apply(x, d)
    ## x = u.right = [8(%rax)], d = b.right = [8(%rsi)]
    movl    8(%rax), %edx                      # x = u.right
    movl    8(%rsi), %esi                      # d = b.right
    jmp     apply                              # tail call

.Lu_leaf:
    ## rule 1: return y = a.right
    movl    8(%rdx), %eax
    ret

.Lb_leaf:
    ## 3a: return w = u.left.right
    movl    4(%rax), %eax                      # eax = u.left (stem-of-w node)
    movl    8(%rax), %eax                      # eax = u.left.right = w
    ret

.La_leaf:
    ## apply(leaf, b) = App(leaf, b) = stem(b)
    movl    %ebx, %edx                         # edx = leaf (rbx)
.La_stem:
    ## apply(stem-node, b) = App(a, b) — a (in edx) is already the stem node
    ## For .La_leaf: edx = leaf.  For .La_stem: edx = a (the stem node itself).
alloc_app:
    pushq   %rdi                               # save new node address
    push    $1
    pop     %rax
    stosl                                      # write tag = 1
    xchg    %edx, %eax
    stosl                                      # write left
    xchg    %esi, %eax
    stosl                                      # write right
    popq    %rax                               # return new node address
    ret

# ==== emit_tree(edx=tree) → minbin on stdout ====
#
# Direct structural emission — the deep app-tree maps 1:1 to minbin:
#   leaf (tag=0)      → '1'
#   App(a, b) (tag=1) → '0' + emit(a) + emit(b)
#
# This is dramatically simpler than the ternary variant's tag-counting loop.
emit_tree:
    movl    (%rdx), %ecx                       # tag (0 or 1)
    jrcxz   .Le_leaf                           # leaf → emit '1'

    ## App node: emit '0', recurse on left, tail-call right
    pushq   %rdx                               # save node
    movb    $'0', %cl
    call    write_byte
    popq    %rdx                               # restore node
    pushq   %rdx                               # save for right child
    movl    4(%rdx), %edx                      # left child
    call    emit_tree                          # emit left subtree
    popq    %rdx                               # restore node
    movl    8(%rdx), %edx                      # right child
    jmp     emit_tree                          # tail call: emit right subtree

.Le_leaf:
    movb    $'1', %cl                          # fall through to write_byte

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

.bss
.lcomm heap, 0x4000000
