# ============================================================
# Code-in-header layout:
#   e_version [20:24] → _start entry: jmp .Linit2 (2B) + padding (2B)
#   p_paddr   [64:70] → exit epilogue: movb $60,%al; xorl %edi,%edi; syscall
#   p_memsz   [80:88] → plain data (64 MB for BSS heap)
#   p_align   [88:96] → init: movl $.Lend,%ebx (5B) + leal 8(%rbx),%edi (3B) — fits exactly
#
# Memory: p_memsz encoded as trampoline code (~15.7GB) with PF_W — zero-filled BSS heap
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

# ---- p_memsz [80:88]: BSS HEAP SIZE ----
    .quad   0x4000000                    # 64 MB zero-filled heap

# ---- p_align [88:96]: INIT CONTINUATION ----
.Linit2:
    movl    $.Lend, %ebx                 # rbx = heap base (BSS start) — 5 bytes
    leal    8(%rbx), %edi                # rdi = free pointer past leaf — 3 bytes

# ================================================================
# Main Code Blob (file offset 96)
# ================================================================

    ## rbp = &apply (call *%rbp = 2B vs call rel32 = 5B)
    movl    $apply, %ebp

    ## No identity — first parsed tree becomes the accumulator.
    ## Behavior is undefined for <2 input trees.
    call    parse_tree
    pushq   %rax

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi                   # 1 byte instead of 2
    call    *%rbp                        # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    jmp     .Lexit                       # exit via p_paddr

## ---- apply(edx=a, esi=b) -> eax ----
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
    movl    4(%rdx), %eax                # u (eax = u addr)
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
    ## rule 2: (x.b).(y.b) where u=stem(x), a=fork(u,y)
    pushq   %rdx                         # save a       [a]
    pushq   %rsi                         # save b       [b][a]
    movl    4(%rax), %edx                # x = u.child
    call    *%rbp                        # apply(x, b) -> eax
    popq    %rsi                         # restore b
    popq    %rdx                         # restore a
    pushq   %rax                         # save x·b     [x·b]
    movl    8(%rdx), %edx                # y = a.right
    call    *%rbp                        # apply(y, b) -> eax
    xchg    %eax, %esi                   # esi = y·b (1B)
    popq    %rdx                         # edx = x·b
    jmp     apply                        # tail call apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.right
    movl    8(%rdx), %eax
    ret

## (alloc_fork/alloc_stem removed — unified into apply body + inlined _start)

## ---- I/O: shared syscall stub ----
write_byte:
    push    $1
    pop     %rax                         # rax=1=SYS_WRITE
do_io:
    pushq   %rdi                         # save free pointer
    movl    %eax, %edi                   # fd = eax
    push    %rcx                         # byte on stack
    mov     %rsp, %rsi                   # buffer = stack
    push    $1
    pop     %rdx
    syscall
    pop     %rcx
    popq    %rdi                         # restore free pointer
    ret

## ---- parse_tree -> eax (SF set on EOF) ----
parse_tree:
    pushq   %rbp
.Lp_read:
    xorl    %eax, %eax                   # eax=0=SYS_READ
    call    do_io
    decl    %eax                         # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al                    # ZF if '0', SF if < '0'
    jz      .Lp_leaf                     # leaf: return .Lend
    js      .Lp_read                     # skip non-digit
    movl    %edi, %ebp
    stosl                                # store tag
    pushq   %rbp
    leaq    (%rdi,%rax,4), %rdi          # pre-bump free pointer past children
    xchg    %eax, %ecx                   # ecx = loop counter
.Lp_loop:
    pushq   %rcx
    call    parse_tree
    popq    %rcx
    addl    $4, %ebp
    movl    %eax, 0(%rbp)
    loop    .Lp_loop
    popq    %rax                         # return base address
    jmp     .Lp_ret
.Lp_leaf:
    movl    %ebx, %eax                   # leaf = .Lend
.Lp_ret:
    popq    %rbp
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    movl    (%rdx), %ecx
    pushq   %rcx
    pushq   %rdx                               # save tree ptr
    addb    $'0', %cl
    call    write_byte
    popq    %rdx                               # restore tree ptr
    popq    %rcx
    jrcxz   1f
.Lemit_loop:
    addl    $4, %edx
    pushq   %rcx
    pushq   %rdx                               # save walker position
    movl    (%rdx), %edx                       # load child pointer
    call    emit_tree
    popq    %rdx                               # restore walker
    popq    %rcx
    loop    .Lemit_loop
1:  ret

.Lend:
