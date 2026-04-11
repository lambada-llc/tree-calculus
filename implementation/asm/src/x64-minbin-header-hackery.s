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
    jmp     apply                        # tail call apply(a, b) → eax

# ==== apply(edx=a, esi=b) -> eax ====
apply:
    movl    (%rdx), %ecx
    cmpl    $2, %ecx
    jae     .La_fork

    ## a=leaf (ecx=0) or a=stem (ecx=1): build [tag+1, ...a.children, b]
    pushq   %rdi                         # save result addr
    leal    1(%rcx), %eax                # tag = a.tag + 1
    stosl                                # write tag
    jrcxz   1f                           # leaf: no children to copy
    movl    4(%rdx), %eax                # a.child (stem case)
    stosl                                # write it
1:  xchg    %esi, %eax                   # eax = b
    stosl                                # append b
    popq    %rax                         # result = start of node
    ret

.La_fork:
    movl    4(%rdx), %eax                # eax = u
    movl    (%rax), %ecx                 # u.tag
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
    pushq   %rdx                         # save a
    pushq   %rsi                         # save b
    movl    4(%rax), %edx                # x = u.child
    call    *%rbp                        # apply(x, b)
    popq    %rsi                         # restore b
    popq    %rdx                         # restore a
    pushq   %rax                         # save x·b
    movl    8(%rdx), %edx                # y = a.right
    call    *%rbp                        # apply(y, b)
    xchg    %eax, %esi                   # esi = y·b
    popq    %rdx                         # edx = x·b
    jmp     apply                        # tail call

.Lu_leaf:
    ## rule 1: y
    movl    8(%rdx), %eax
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
    mov     %rsp, %rsi                   # buffer = stack
    push    $1
    pop     %rdx                         # count = 1
    syscall
    pop     %rcx                         # read result in cl / clean up
    popq    %rdi                         # restore free pointer
    ret

# ==== emit_tree(edx=tree) → minbin on stdout ====
emit_tree:
    movl    (%rdx), %ecx                 # ecx = tag (0, 1, or 2)
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
    movl    4(%rdx), %edx                # first child
    call    emit_tree
    popq    %rdx                         # restore node
    popq    %rcx
    jrcxz   1f                           # stem: done after one child
    movl    8(%rdx), %edx                # fork right child
    jmp     emit_tree                    # tail call

.Le_done:
    popq    %rdx                         # clean up entry push
1:  ret

.Lend:
