# ============================================================
# Memory representation: application trees rather than ternary nodes.
# Only two node types in the heap:
#   leaf:  [0]                — 4 bytes
#   app:   [1] [left] [right] — 12 bytes
# Ternary forms are encoded as nested apps:
#   stem(x)    = App(leaf, x)
#   fork(x, y) = App(App(leaf, x), y)
#
# Code-in-header layout:
#   e_version [20:24] → _start entry: jmp .Linit2 (2B) + padding (2B)
#   p_paddr   [64:70] → exit epilogue: movb $60,%al; xorl %edi,%edi; syscall
#   p_align   [88:96] → init: movl $.Lend,%ebx (5B) + leal 4(%rbx),%edi (3B) — fits exactly
#
# Build:
#   as main_elf.s -o main.o
#   ld -Ttext=0x400000 main.o -o main.elf
#   objcopy -O binary -j .text main.elf main
#   chmod +x main
# ============================================================

.text
.globl _start

# ================================================================
# ELF64 Header (64 bytes, file offset 0)
# ================================================================
ehdr:
    .byte   0x7f, 'E', 'L', 'F'        # [0:4]   e_ident: magic
    .byte   2, 1, 1, 0                  # [4:8]   64-bit, LE, v1, OSABI=0
    .quad   0                            # [8:16]  EI_PAD (must be 0 for Rosetta)
    .word   2                            # [16:18] e_type  = ET_EXEC
    .word   62                           # [18:20] e_machine = EM_X86_64

# ---- e_version [20:24]: ENTRY POINT — first init instructions ----
_start:
    jmp     .Linit2                      # → p_align for heap init (2 bytes)
    .skip   2                            # [22:24] padding

    .quad   _start                       # [24:32] e_entry (linker resolves vaddr)
    .quad   phdr - ehdr                  # [32:40] e_phoff = 40

# ================================================================
# ELF64 Program Header (56 bytes at offset 40, overlaps ehdr[40:64])
# ================================================================
phdr:
    .int    1                            # [40:44] p_type  = PT_LOAD
    .int    7                            # [44:48] p_flags = PF_R | PF_W | PF_X
    .quad   1                            # [48:56] p_offset = 1
    .quad   0x400001                     # [56:64] p_vaddr  (low 2 bytes → e_phnum=1)

# ---- p_paddr [64:72]: EXIT EPILOGUE ----
.Lexit:
    movb    $60, %al                     # SYS_EXIT (2 bytes)
    xorl    %edi, %edi                   # status = 0 (2 bytes)
    syscall                              # exit (2 bytes)
    .skip   2                            # [70:72] pad to 8 bytes

    .quad   .Lend - ehdr - 1            # [72:80] p_filesz = file_size - p_offset
    .quad   0x4000000                    # [80:88] p_memsz  = 64 MB (BSS heap)

# ---- p_align [88:96]: INIT CONTINUATION ----
.Linit2:
    movl    $.Lend, %ebx                 # rbx = heap base (BSS start) — 5 bytes
    leal    4(%rbx), %edi                # rdi = free pointer past leaf — 3 bytes

# ================================================================
# Main Code Blob (file offset 96)
# ================================================================

    ## rbx = .Lend (leaf addr); rdi = free pointer past 4-byte leaf node
    movl    $apply, %ebp                 # rbp = &apply (call *%rbp = 2B vs 5B)

    call    parse_eval                   # parse + eval entire stdin → eax
    xchg    %eax, %edx                   # edx = result (1 byte vs 2)
    call    emit_tree                    # emit result in minbin
    jmp     .Lexit                       # exit via p_paddr

# ==== parse_eval -> eax (tree pointer) ====
parse_eval:
.Lpe_read:
    xorl    %eax, %eax                   # eax=0=SYS_READ, fd=stdin
    call    do_io
    decl    %eax                         # 1 → 0 (byte read), else → eof/error
    jnz     .Lpe_leaf                    # EOF: return leaf
    subb    $'0', %cl           # cl = char - '0'; CF if < '0'
    jb      .Lpe_read           # < '0': skip whitespace
    je      .Lpe_apply          # '0': application
    decb    %cl                 # was '1'? (cl → 0)
    jnz     .Lpe_read           # > '1': skip
                                # fallthrough: '1' → leaf

.Lpe_leaf:
    movl    %ebx, %eax                   # leaf = .Lend
    ret

.Lpe_apply:
    call    parse_eval                   # a = parse first subexpr
    pushq   %rax                         # save a
    call    parse_eval                   # b = parse second subexpr
    xchg    %eax, %esi                   # esi = b
    popq    %rdx                         # edx = a
    jmp     apply                        # tail call apply(a, b) → eax

# ==== apply(edx=a, esi=b) -> eax ====
#
# Deep app-tree pattern matching:
#   leaf       = tag 0
#   stem(x)    = App(leaf, x)          — tag 1, left.tag 0
#   fork(u, y) = App(App(leaf, u), y)  — tag 1, left.tag 1
apply:
    movl    (%rdx), %ecx
    jrcxz   .La_leaf                           # a = leaf (tag 0)

    ## a is App(a.left, a.right) — check a.left for stem vs fork
    movl    4(%rdx), %eax                      # eax = a.left
    movl    (%rax), %ecx                       # a.left.tag
    jrcxz   .La_stem                           # a.left = leaf → a is stem-like

    ## a is fork-like: a = App(App(leaf, u), y)
    movl    8(%rax), %eax                      # eax = u = a.left.right
    movl    (%rax), %ecx                       # u.tag
    jrcxz   .Lu_leaf                           # u = leaf → rule 1

    ## u is App — check u.left for stem-like vs fork-like
    movl    4(%rax), %ecx                      # ecx = u.left (pointer)
    movl    (%rcx), %ecx                       # ecx = u.left.tag
    jrcxz   .Lu_stem                           # u.left = leaf → u = stem → rule 2

    ## u = fork(w, x): triage on b
    movl    (%rsi), %ecx
    jrcxz   .Lb_leaf                           # b = leaf → rule 3a

    ## b is App — check b.left for stem-like vs fork-like
    movl    4(%rsi), %ecx                      # ecx = b.left (pointer)
    movl    (%rcx), %ecx                       # ecx = b.left.tag
    jrcxz   .Lb_stem                           # b.left = leaf → b = stem → rule 3b

    ## 3c: b = fork(c, d). apply(apply(y, c), d)
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
    ## rule 2: apply(apply(x, b), apply(y, b))
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
    ## 3b: apply(x, d). x = u.right, d = b.right
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
    ## apply(stem-node, b) = App(a, b)
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
write_byte:
    push    $1
    pop     %rax                         # rax=1=SYS_WRITE
do_io:
    pushq   %rdi                         # save free pointer
    movl    %eax, %edi                   # fd
    push    %rcx                         # byte on stack
    mov     %rsp, %rsi                   # buffer = stack
    push    $1
    pop     %rdx                         # count = 1
    syscall
    pop     %rcx                         # read result in cl / clean up
    popq    %rdi                         # restore free pointer
    ret

.Lend:
