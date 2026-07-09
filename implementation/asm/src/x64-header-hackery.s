# ============================================================
# Code-in-header layout:
#   e_version [20:24] → _start entry: jmp .Linit2 (2B) + padding (2B)
#   p_paddr   [64:70] → exit epilogue: movb $60,%al; xorl %edi,%edi; syscall
#   p_memsz   [80:88] → trampoline: push $2; pop %rax (3B) + jmp .Linit2 (2B) + pad (3B)
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

    ## rbx = .Lend (leaf addr = [0][0] from BSS); rdi = free pointer past leaf node
    ## eax = 2 (from p_memsz trampoline) — unused by the tagless build, overwritten below

    ## Build identity: fork(fork(leaf, leaf), leaf) — inlined.
    ## Two-word layout: a fork is just [left][right], no tag word.
    movl    %ebx, %eax                   # eax = leaf (discards the =2 from the trampoline)
    movl    %edi, %ebp                   # ebp = inner fork addr
    stosl                                # inner.u = leaf
    stosl                                # inner.v = leaf
    pushq   %rdi                         # push outer fork addr (= result)
    xchg    %ebp, %eax                   # eax = inner fork addr, ebp = leaf
    stosl                                # outer.u = inner
    xchg    %ebp, %eax                   # eax = leaf, ebp = inner
    stosl                                # outer.v = leaf
    movl    $apply, %ebp                 # rbp = &apply (call *%rbp = 2B vs 5B)

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi                   # 1 byte instead of 2
    call    *%rbp
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    jmp     .Lexit                       # exit via p_paddr

## ---- apply(edx=a, esi=b) -> eax ----
apply:
    movl    (%rdx), %eax               # eax = a.u
    movl    4(%rdx), %ecx              # ecx = a.v
    jrcxz   .La_build                  # a.v == 0 -> a is leaf or stem

    ## a = fork(u, y): u = a.u (eax), y = a.v.
    movl    (%rax), %ecx               # u.u
    jrcxz   .Lu_leaf
    movl    4(%rax), %ecx              # u.v
    jrcxz   .Lu_stem

    ## u = fork(w, x): triage on b. w=u.u, x=u.v; y=a.v.
    ##   b=leaf -> w ; b=stem(z) -> x·z ; b=fork(p,q) -> y·p·q
    movl    (%rsi), %ecx               # b.u
    jrcxz   .Lb_leaf
    movl    4(%rsi), %ecx              # b.v
    jrcxz   .Lb_stem
.Lb_fork:
    movl    4(%rsi), %eax              # q = b.v
    pushq   %rax                       # save q
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
    pushq   %rdx                         # save a       [a]
    pushq   %rsi                         # save b       [b][a]
    movl    (%rax), %edx                 # x = u.u
    call    *%rbp                        # apply(x, b) -> eax
    popq    %rsi                         # restore b
    popq    %rdx                         # restore a
    pushq   %rax                         # save x·b     [x·b]
    movl    4(%rdx), %edx                # y = a.v
    call    *%rbp                        # apply(y, b) -> eax
    xchg    %eax, %esi                   # esi = y·b (1B)
    popq    %rdx                         # edx = x·b
    jmp     *%rbp                        # tail apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> build stem(b)=[b][0]; a=stem(x) -> build fork(x,b)=[x][b].
    testl   %eax, %eax                 # a.u == 0 -> leaf
    jnz     1f
    xchg    %eax, %esi                 # leaf: eax=b, esi=0 (old a.u)
1:  pushq   %rdi                       # save result addr
    stosl                              # write u = eax
    xchg    %eax, %esi                 # eax = v
    stosl                              # write v
    popq    %rax                       # result = start of node
    ret

## ---- I/O: shared syscall stub ----
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

## ---- parse_tree -> eax (SF set on EOF) ----
parse_tree:
.Lp_read:
    xorl    %eax, %eax                   # eax=0=SYS_READ
    call    do_io
    decl    %eax                         # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al                    # ZF if '0', SF if < '0'
    jz      .Lp_leaf                     # leaf: return .Lend
    js      .Lp_read                     # skip non-digit
    xchg    %eax, %ecx                   # ecx = child count (1 or 2)
    movl    %edi, %edx                   # edx = node base
    pushq   %rdx
    scasq                                # reserve two words (u, v): rdi += 8 in 2 bytes
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    parse_tree
    popq    %rdx
    popq    %rcx
    movl    %eax, (%rdx)                 # store child
    addl    $4, %edx                     # next slot
    loop    .Lp_loop
    popq    %rax                         # return base address
    ret
.Lp_leaf:
    movl    %ebx, %eax                   # leaf = .Lend
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless.
    push    $2
    pop     %rcx
    cmpl    $1, (%rdx)                         # CF = (u == 0)
    sbbl    $0, %ecx
    cmpl    $1, 4(%rdx)                        # CF = (v == 0)
    sbbl    $0, %ecx                           # ecx = tag in {0,1,2} = child count
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
