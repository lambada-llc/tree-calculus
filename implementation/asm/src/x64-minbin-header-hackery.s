# ============================================================
# Code-in-header layout:
#   e_version [20:24] → _start entry: jmp .Linit2 (2B) + padding (2B)
#   p_paddr   [64:70] → exit epilogue: movb $60,%al; xorl %edi,%edi; syscall
#   p_align   [88:96] → init: movl $.Lend,%ebx (5B) + leal 8(%rbx),%edi (3B) — fits exactly
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
    leal    8(%rbx), %edi                # rdi = free pointer past leaf — 3 bytes

# ================================================================
# Main Code Blob (file offset 96)
# ================================================================

    ## rbx = .Lend (leaf addr); rdi = free pointer past leaf node
    movl    $apply, %ebp                 # rbp = &apply (call *%rbp = 2B vs 5B)

    call    parse_eval                   # parse + eval entire stdin → eax
    xchg    %eax, %edx                   # edx = result (1 byte vs 2)
    call    emit_tree                    # emit result in minbin
    jmp     .Lexit                       # exit via p_paddr

# ==== parse_eval -> eax (tree offset) ====
parse_eval:
.Lpe_read:
    xorl    %eax, %eax                   # eax=0=SYS_READ, fd=stdin
    call    do_io
    decl    %eax                         # 1 → 0 (byte read), else → eof/error
    jnz     .Lpe_leaf                    # EOF: return leaf
    subb    $'0', %cl                    # cl = char - '0'; CF if < '0'
    jb      .Lpe_read                    # < '0': skip whitespace
    je      .Lpe_apply                   # '0': application
    decb    %cl                          # was '1'? (cl → 0)
    jnz     .Lpe_read                    # > '1': skip
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
    jmp     *%rbp                        # tail apply(a, b)  (rbp = &apply)

# ==== apply(edx=a, esi=b) -> eax ====  (tagless two-word triage)
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
    movl    (%rax), %edx                 # x = u.u
    call    *%rbp                        # apply(x, b)
    popq    %rsi
    popq    %rdx
    pushq   %rax                         # save x·b
    movl    4(%rdx), %edx                # y = a.v
    call    *%rbp                        # apply(y, b)
    xchg    %eax, %esi                   # esi = y·b
    popq    %rdx                         # edx = x·b
    jmp     *%rbp                        # tail apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].
    testl   %eax, %eax
    jnz     1f
    xchg    %eax, %esi
1:  pushq   %rdi
    stosl                                # write u
    xchg    %eax, %esi
    stosl                                # write v
    popq    %rax
    ret

## (alloc_fork/alloc_stem removed — unified into apply body)

# ==== I/O ====
write_byte:
    push    $1
    pop     %rax                         # rax=1=SYS_WRITE
do_io:
    pushq   %rdi                         # save free pointer
    movl    %eax, %edi                   # fd
    push    %rcx                         # byte on stack
    push    %rsp
    pop     %rsi                         # buffer = stack
    push    $1
    pop     %rdx                         # count = 1
    syscall
    pop     %rcx                         # read result in cl / clean up
    popq    %rdi                         # restore free pointer
    ret

# ==== emit_tree(edx=tree) → minbin on stdout ====
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless via the heap-base threshold (rbx).
    cmpl    %ebx, (%rdx)
    sbbl    %ecx, %ecx
    cmpl    %ebx, 4(%rdx)
    sbbl    $-2, %ecx                    # ecx = tag (0, 1, or 2)
    pushq   %rdx                         # save node for function lifetime

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
    jrcxz   .Le_done                     # leaf: no children
    popq    %rdx                         # restore node from entry
    decl    %ecx
    pushq   %rcx                         # save (tag-1)
    pushq   %rdx                         # save node for right child
    movl    (%rdx), %edx                 # first child (offset 0)
    call    emit_tree
    popq    %rdx                         # restore node
    popq    %rcx
    jrcxz   1f                           # stem: done after one child
    movl    4(%rdx), %edx                # fork right child (offset 4)
    jmp     emit_tree                    # tail call

.Le_done:
    popq    %rdx                         # clean up entry push
1:  ret

.Lend:
