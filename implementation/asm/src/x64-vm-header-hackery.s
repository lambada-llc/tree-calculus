# ============================================================
# Code-in-header layout:
#   e_version [20:24] → _start entry: jmp .Linit2 (2B) + padding (2B)
#   p_paddr   [64:70] → exit epilogue: movb $60,%al; xorl %edi,%edi; syscall
#   p_memsz   [80:88] → trampoline: push $2; pop %rax (3B) + jmp .Linit2 (2B) + pad (3B)
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
    jmp     .Lpmem                       # → p_memsz trampoline (2 bytes)
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

# ---- p_memsz [80:88]: CODE IN HEADER — identity setup preamble ----
.Lpmem:
    push    $2
    pop     %rax                         # eax = 2 (for stosl tag) — 3 bytes
    jmp     .Linit2                      # → p_align for heap init — 2 bytes
    .skip   3                            # [85:88] pad (high bytes of p_memsz)

# ---- p_align [88:96]: INIT CONTINUATION ----
.Linit2:
    movl    $.Lend, %ebx                 # rbx = heap base (BSS start) — 5 bytes
    leal    8(%rbx), %edi                # rdi = free pointer past leaf — 3 bytes

# ================================================================
# Main Code Blob (file offset 96)
# ================================================================

    ## rbx = .Lend (leaf addr); rdi = free pointer past leaf node
    ## eax = 2 (from p_memsz trampoline)

    ## Build identity: fork(fork(leaf, leaf), leaf) — inlined
    movl    %edi, %ebp                   # ebp = inner fork addr
    stosl                                # inner.tag = 2
    xchg    %ebx, %eax                   # eax = leaf, ebx = 2 (temp)
    stosl                                # inner.left = leaf
    stosl                                # inner.right = leaf
    pushq   %rdi                         # push outer fork addr (= result)
    xchg    %ebx, %eax                   # eax = 2, ebx = leaf (restored)
    stosl                                # outer.tag = 2
    xchg    %ebp, %eax                   # eax = inner fork addr
    stosl                                # outer.left = inner
    movl    %ebx, %eax
    stosl                                # outer.right = leaf

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi                   # 1 byte instead of 2
    call    apply
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    jmp     .Lexit                       # exit via p_paddr

## (alloc_fork/alloc_stem removed — unified into .Lreduce body + inlined _start)

## ---- apply(edx=a, esi=b) -> eax ----
apply:
    pushq   $-1                          # sentinel

.Lreduce:
    movl    (%rdx), %ecx
    cmpl    $2, %ecx
    jae     .Lvm_a_fork

    ## a=leaf (ecx=0) or a=stem (ecx=1): build [tag+1, ...a.children, b]
    pushq   %rdi                         # save result addr
    leal    1(%rcx), %eax                # tag = a.tag + 1
    stosl                                # write tag
    jrcxz   1f                           # leaf: no children to copy
    movl    4(%rdx), %eax                # a.child (stem case)
    stosl                                # write it
1:  xchg    %esi, %eax                   # eax = b
    stosl                                # append b
    popq    %rax                         # result
    jmp     .Ldispatch

.Lvm_a_fork:
    movl    4(%rdx), %eax                # eax = u = a.left
    movl    (%rax), %ecx                 # u.tag
    jrcxz   .Lvm_u_leaf
    decl    %ecx
    jz      .Lvm_u_stem

    ## ---- u = fork(w, x): triage on b ----
    movl    (%rsi), %ecx
    jrcxz   .Lvm_b_leaf
    decl    %ecx
    jz      .Lvm_b_stem

    ## b = fork(d, e):  apply(apply(y, d), e)
    movl    8(%rsi), %eax                # eax = e = b.right
    movl    8(%rdx), %edx                # edx = y = a.right
    movl    4(%rsi), %esi                # esi = d = b.left
.Lpush_at_reduce:
    pushq   %rax                         # push e (or result)
    pushq   $0                           # tag = APPLY_TO
    jmp     .Lreduce

.Lvm_u_stem:
    ## u = stem(u'):  apply(apply(u', b), apply(y, b))
    movl    4(%rax), %eax                # eax = u' = u.child
    pushq   %rsi                         # arg2 = b
    pushq   %rax                         # arg1 = u'
    pushq   $1                           # tag = COMPUTE_AND_APPLY
    movl    8(%rdx), %edx                # a = y = a.right
    jmp     .Lreduce

.Lvm_b_stem:
    ## b = stem(d):  apply(x, d)
    movl    8(%rax), %edx                # a = x = u.right
    movl    4(%rsi), %esi                # b = d = b.child
    jmp     .Lreduce

    ## (Lvm_a_leaf/Lvm_a_stem removed — unified above)

.Lvm_u_leaf:
    ## apply(fork(leaf, y), b) = y
    movl    8(%rdx), %eax
    jmp     .Ldispatch

.Lvm_b_leaf:
    ## b = leaf:  result = w = u.left
    movl    4(%rax), %eax
    ## fall through

.Ldispatch:
    popq    %rcx                         # frame tag: -1=sentinel, 0=AT, 1=CAA
    jrcxz   .Lvm_at                      # tag 0 -> APPLY_TO
    incl    %ecx
    jz      .Lvm_done                    # -1+1=0 -> sentinel, done

    ## COMPUTE_AND_APPLY
    popq    %rdx                         # fn -> a
    popq    %rsi                         # arg -> b
    jmp     .Lpush_at_reduce

.Lvm_at:
    ## APPLY_TO: a = result, b = arg
    popq    %rsi                         # b = arg
    xchg    %eax, %edx                   # a = result
    jmp     .Lreduce

.Lvm_done:
    ret

## ---- I/O: shared syscall ----
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

## ---- parse_tree -> eax ----
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

## ---- emit_tree(edx=tree) ----
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
