# ============================================================
# Hand-crafted, *Linux-valid* ELF for the tagless two-word x64 evaluator.
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
#   as x64-header-hackery.s -o x.o && ld -Ttext=0x400000 x.o -o x.elf
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
    leaq    apply(%rip), %rbp            # rbp = &apply
    .byte   0x00, 0xc9                   # addb %cl,%cl — window filler, harmless

# ---- p_align [96:104] is kernel-ignored for ET_EXEC: code just flows ----
    .byte   0x8d, 0x7d                   # leal disp8(%rbp), %edi
    .byte   leaf-apply                   #   rdi = leaf = heap base ([0][0] from BSS)

    ## Build identity: fork(fork(leaf, leaf), leaf) — two-word layout.
    movl    %edi, %eax                   # eax = leaf
    scasq                                # rdi = leaf+8 = free pointer
    movl    %edi, %esi                   # esi = inner fork addr
    stosl                                # inner.u = leaf
    stosl                                # inner.v = leaf
    pushq   %rdi                         # push outer fork addr (= result)
    xchg    %esi, %eax                   # eax = inner
    stosl                                # outer.u = inner
    xchg    %esi, %eax                   # eax = leaf
    stosl                                # outer.v = leaf

1:  call    parse_tree
    popq    %rdx                         # accumulator (pop before EOF test; pop leaves flags)
    js      2f
    xchg    %eax, %esi
    call    *%rbp                        # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  ## Retarget the dead rbp (= &apply) to emit_tree.
    .byte   0x8d, 0x6d                   # leal disp8(%rbp), %ebp
    .byte   emit_tree-apply
    call    *%rbp                        # emit_tree(edx = accumulator)
    movb    $60, %al                     # SYS_EXIT
    xorl    %edi, %edi
    syscall

## ---- do_io tail (head lives in the e_shoff/p_paddr islands) ----
.Ldo_io3:
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
    call    parse_tree
    popq    %rdx
    popq    %rcx
    movl    %eax, (%rdx)                 # store child
    addl    $4, %edx                     # next slot
    loop    .Lp_loop
.Lp_done:
    popq    %rax                         # return base address
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless: rbp (= &emit_tree here) is a
    ## code address strictly between 0 and every heap pointer.
    cmpl    %ebp, (%rdx)                 # CF = (u == 0)
    sbbl    %ecx, %ecx                   # ecx = -(u == 0)
    cmpl    %ebp, 4(%rdx)                # CF = (v == 0)
    sbbl    $-2, %ecx                    # ecx = tag in {0,1,2} = child count
    pushq   %rcx
    pushq   %rdx
    addb    $'0', %cl
    call    write_byte
    popq    %rdx
    popq    %rcx
    jrcxz   1f
.Lemit_loop:
    pushq   %rcx
    pushq   %rdx
    movl    (%rdx), %edx                 # child = *slot (offset 0 then 4)
    call    *%rbp                        # emit_tree (rbp retargeted to it)
    popq    %rdx
    popq    %rcx
    addl    $4, %edx                     # next slot
    loop    .Lemit_loop
1:  ret

## ---- apply(edx=a, esi=b) -> eax ----  (last: leaf sits just past it)
apply:
    movl    (%rdx), %eax                 # eax = a.u
    movl    4(%rdx), %ecx                # ecx = a.v
    jrcxz   .La_build                    # a.v == 0 -> a is leaf or stem

    movl    (%rax), %ecx                 # u.u
    jrcxz   .Lu_leaf
    movl    4(%rax), %ecx                # u.v
    jrcxz   .Lu_stem

    ## u = fork(w, x): triage on b.  b=leaf->w ; b=stem(z)->x·z ; b=fork(p,q)->y·p·q
    movl    (%rsi), %ecx                 # b.u
    jrcxz   .Lb_leaf
    movl    4(%rsi), %ecx                # b.v  (== q, kept for .Lb_fork)
    jrcxz   .Lb_stem
.Lb_fork:
    pushq   %rcx                         # save q = b.v
    movl    (%rsi), %esi                 # p = b.u
    movl    4(%rdx), %edx                # y = a.v
    call    *%rbp                        # apply(y, p) -> eax
    popq    %rsi                         # esi = q
    xchg    %eax, %edx                   # edx = y·p
    jmp     *%rbp                        # tail apply(y·p, q)
.Lb_stem:
    movl    4(%rax), %edx                # x = u.v
    movl    (%rsi), %esi                 # z = b.u
    jmp     *%rbp                        # tail apply(x, z)
.Lb_leaf:
    movl    (%rax), %eax                 # w = u.u
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
    xchg    %eax, %esi                   # leaf: eax=b, esi=0
1:  pushq   %rdi
    stosl                                # write u
    xchg    %eax, %esi
    stosl                                # write v
    popq    %rax
    ret

## Canonical leaf = first byte past the file = [0][0] via kernel BSS
## zero-fill. apply stays within 127 bytes of leaf (disp8 references).
leaf:
.Lend:
