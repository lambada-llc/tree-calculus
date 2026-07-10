# ============================================================
# Hand-crafted, *Linux-valid* ELF (deep app-tree representation, minbin I/O).
#
# Unlike the old header-hackery layout (phdr at 40, e_phentsize=0 — loadable
# only under Rosetta), every field the Linux kernel actually validates holds
# its required value here, so this binary execs on a stock kernel:
#
#   - phdr at offset 48, overlapping the ehdr tail. The kernel-read fields
#     there are e_phentsize (=56, doubled by p_flags' high half; the loader
#     only inspects the PF_R/W/X bits) and e_phnum (=1, doubled by
#     p_offset=1's low bytes). e_flags/e_ehsize/e_sh* absorb the rest.
#   - p_offset=1 with p_vaddr=0x400001: file offset F maps to 0x400000+F,
#     and offset ≡ vaddr (mod page) keeps Rosetta happy too.
#
# Code lives in every hole the kernel ignores:
#   e_shoff  [40:48] — write_byte + do_io head        (jmp → p_paddr)
#   p_paddr  [72:80] — do_io argument setup           (jmp → do_io tail)
#   p_memsz  [88:96] — _start's first 8 bytes: lea+00, whose LE value
#                      (~2.2 GB) doubles as a valid "big enough" memsz
#   p_align  [96:..] — ignored for ET_EXEC; code flows contiguously from 96
#
# Build:
#   as x64-minbin-deep-header-hackery.s -o x.o && ld -Ttext=0x400000 x.o -o x.elf
#   objcopy -O binary -j .text x.elf x && chmod +x x
# ============================================================

.text
.globl _start

# ================================================================
# ELF64 Header
# ================================================================
ehdr:
    .byte   0x7f, 'E', 'L', 'F'          # [0:4]   magic
    .byte   2, 1, 1, 0                   # [4:8]   64-bit, LE, v1, OSABI=0
    .quad   0                            # [8:16]  EI_PAD (0 for Rosetta)
    .word   2                            # [16:18] e_type    = ET_EXEC
    .word   62                           # [18:20] e_machine = EM_X86_64
    .int    1                            # [20:24] e_version
    .quad   _start                       # [24:32] e_entry (= 0x400058)
    .quad   48                           # [32:40] e_phoff

# ---- e_shoff [40:48] (kernel-ignored): write_byte + do_io head ----
write_byte:
    push    $1
    pop     %rax                         # rax = 1 = SYS_WRITE
do_io:
    pushq   %rdi                         # save free pointer
    movl    %eax, %edi                   # fd = eax (0 read / 1 write)
    jmp     .Ldo_io2                     # continue in p_paddr

# ================================================================
# Program Header (offset 48, overlapping ehdr[48:64])
# ================================================================
.org 48
    .int    1                            # [48:52] p_type  = PT_LOAD (= e_flags, ignored)
    .int    0x00380007                   # [52:56] p_flags = RWX; high half = e_phentsize = 56
    .quad   1                            # [56:64] p_offset = 1; low bytes = e_phnum = 1
    .quad   0x400001                     # [64:72] p_vaddr (≡ p_offset mod page)

# ---- p_paddr [72:80] (kernel-ignored): do_io argument setup ----
.Ldo_io2:
    push    %rcx                         # byte on stack (write: cl=data; read: overwritten)
    push    %rsp
    pop     %rsi                         # buffer = stack
    push    $1
    pop     %rdx                         # count = 1
    jmp     .Ldo_io3                     # continue in the main stream

.org 80
    .quad   .Lend - ehdr - 1             # [80:88] p_filesz

# ---- p_memsz [88:96]: _start's first 8 bytes double as the value ----
# lea's disp32 high bytes are 00 00 00 and the filler byte closes the
# window: value = 0x__2d8d48 | disp<<24 ≈ 2.2 GB — big enough for the
# heap, small enough to map.
.org 88
_start:
    leaq    apply(%rip), %rbp   # rbp = &apply (first: the lea disp32 high
                                # bytes 00 00 complete the p_memsz window)
    .byte   0x00, 0xc9          # addb %cl,%cl — window filler, harmless
    leaq    .Lend(%rip), %rbx   # rbx = heap base = leaf address
    leal    4(%rbx), %edi       # rdi = free pointer, skip 4-byte leaf@0

    call    parse_eval          # parse + eval entire stdin → eax
    xchg    %eax, %edx          # edx = result (1 byte vs 2)
    call    emit_tree           # emit result in minbin

    movb    $60, %al
    xorl    %edi, %edi
    syscall

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
    movb    $'1', %cl
    jmp     write_byte                         # rel8: island is within -128 (emit sits early)

## ---- do_io tail (head lives in the e_shoff/p_paddr islands) ----
.Ldo_io3:
    syscall
    pop     %rcx
    popq    %rdi                         # restore free pointer
    ret

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
.Lend:
