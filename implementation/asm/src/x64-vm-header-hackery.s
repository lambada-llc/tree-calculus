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

    ## Build identity: fork(fork(leaf, leaf), leaf) — tagless two-word forks.
    ## eax = 2 from the p_memsz trampoline is dead here; overwritten below.
    movl    %ebx, %eax                   # eax = leaf
    movl    %edi, %edx                   # edx = inner fork addr
    stosl                                # inner.u = leaf
    stosl                                # inner.v = leaf
    pushq   %rdi                         # push outer fork addr (= result)
    xchg    %edx, %eax                   # eax = inner (edx = leaf)
    stosl                                # outer.u = inner
    xchg    %edx, %eax                   # eax = leaf (edx = inner)
    stosl                                # outer.v = leaf
    movl    $parse_tree, %ebp            # rbp = &parse_tree (call *%rbp = 2B)

1:  call    *%rbp                        # parse_tree
    popq    %rdx                         # accumulator (pop before EOF test; leaves flags)
    js      2f
    xchg    %eax, %esi                   # 1 byte instead of 2
    call    apply
    pushq   %rax
    jmp     1b

2:  call    emit_tree
    jmp     .Lexit                       # exit via p_paddr

## ---- apply(edx=a, esi=b) -> eax ----  (tagless two-word, VM continuation stack)
apply:
    pushq   $-1                          # sentinel
.Lreduce:
    movl    (%rdx), %eax                 # a.u
    movl    4(%rdx), %ecx                # a.v
    jrcxz   .Lvm_a_build                 # a.v == 0 -> a is leaf or stem

    movl    (%rax), %ecx                 # u.u
    jrcxz   .Lvm_u_leaf
    movl    4(%rax), %ecx                # u.v
    jrcxz   .Lvm_u_stem

    ## u = fork(w, x): triage on b
    movl    (%rsi), %ecx                 # b.u
    jrcxz   .Lvm_b_leaf
    movl    4(%rsi), %ecx                # b.v
    jrcxz   .Lvm_b_stem

    ## b = fork(d, e):  apply(apply(y, d), e)
    movl    4(%rsi), %eax                # e = b.v
    movl    4(%rdx), %edx                # y = a.v
    movl    (%rsi), %esi                 # d = b.u
.Lpush_at_reduce:
    pushq   %rax                         # push e (or result)
    pushq   $0                           # tag = APPLY_TO
    jmp     .Lreduce

.Lvm_u_stem:
    ## u = stem(u'):  apply(apply(u', b), apply(y, b))
    movl    (%rax), %eax                 # u' = u.u
    pushq   %rsi                         # arg2 = b
    pushq   %rax                         # arg1 = u'
    pushq   $1                           # tag = COMPUTE_AND_APPLY
    movl    4(%rdx), %edx                # a = y = a.v
    jmp     .Lreduce

.Lvm_b_stem:
    ## b = stem(d):  apply(x, d)
    movl    4(%rax), %edx                # a = x = u.v
    movl    (%rsi), %esi                 # b = d = b.u
    jmp     .Lreduce

.Lvm_u_leaf:
    ## apply(fork(leaf, y), b) = y = a.v
    movl    4(%rdx), %eax
    jmp     .Ldispatch

.Lvm_b_leaf:
    ## b = leaf:  result = w = u.u
    movl    (%rax), %eax
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

.Lvm_a_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].  Then dispatch.
    testl   %eax, %eax
    jnz     1f
    xchg    %eax, %esi                   # leaf: eax=b, esi=0
1:  pushq   %rdi
    stosl                                # write u
    xchg    %eax, %esi
    stosl                                # write v
    popq    %rax
    jmp     .Ldispatch

## ---- I/O: shared syscall ----
write_byte:
    push    $1
    pop     %rax                         # rax=1=SYS_WRITE
do_io:
    pushq   %rdi                         # save free pointer
    movl    %eax, %edi                   # fd = eax
    push    %rcx                         # byte on stack
    push    %rsp
    pop     %rsi                         # buffer = stack
    push    $1
    pop     %rdx
    syscall
    pop     %rcx
    popq    %rdi                         # restore free pointer
    ret

## ---- parse_tree -> eax ----
parse_tree:
.Lp_read:
    xorl    %eax, %eax                   # eax=0=SYS_READ
    call    do_io
    decl    %eax                         # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al                    # '0'->0, '1'->1, '2'->2, whitespace->negative
    js      .Lp_read                     # skip non-digit
    movl    %eax, %ecx                   # ecx = child count (0,1,2); eax stays 0/1/2 so
                                         # scasq leaves SF clear (caller's EOF test is js)
    movl    %edi, %edx                   # edx = node base
    pushq   %rdx
    scasq                                # reserve two words (u, v)
    jrcxz   .Lp_done                     # count 0 -> leaf: the reserved [0][0] node is it
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    *%rbp                        # parse_tree
    popq    %rdx
    popq    %rcx
    movl    %eax, (%rdx)                 # store child
    addl    $4, %edx                     # next slot
    loop    .Lp_loop
.Lp_done:
    popq    %rax                         # return base address
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) ----
emit_tree:
    cmpl    %ebx, (%rdx)                       # CF = (u == 0)
    sbbl    %ecx, %ecx                         # ecx = -(u == 0)
    cmpl    %ebx, 4(%rdx)                      # CF = (v == 0)
    sbbl    $-2, %ecx                          # ecx = tag in {0,1,2} = child count
    pushq   %rcx
    pushq   %rdx                               # save tree ptr
    addb    $'0', %cl
    call    write_byte
    popq    %rdx                               # restore tree ptr
    popq    %rcx
    jrcxz   1f
.Lemit_loop:
    pushq   %rcx
    pushq   %rdx                               # save walker position
    movl    (%rdx), %edx                       # child = *slot (offset 0 then 4)
    call    emit_tree
    popq    %rdx                               # restore walker
    popq    %rcx
    addl    $4, %edx                           # next slot
    loop    .Lemit_loop
1:  ret

.Lend:
