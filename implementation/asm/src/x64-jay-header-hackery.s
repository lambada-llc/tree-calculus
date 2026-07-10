# ============================================================
# Hand-crafted, *Linux-valid* ELF (tagless two-word nodes, Jay rules).
#
# Same layout as x64-header-hackery.s: phdr at offset 48 overlapping the
# ehdr tail (e_phentsize=56 doubled by p_flags' high half, e_phnum=1 by
# p_offset=1), p_offset=1/p_vaddr=0x400001 so file offset F maps to
# 0x400000+F, and code in every kernel-ignored hole: write_byte + do_io
# head in e_shoff [40:48] and p_paddr [72:80], _start's first 8 bytes in
# p_memsz [88:96] (their LE value ~2.2 GB doubles as the heap size), and
# everything after flowing from p_align [96:...].
#
# Build:
#   as x64-jay-header-hackery.s -o x.o && ld -Ttext=0x400000 x.o -o x.elf
#   objcopy -O binary -j .text x.elf x && chmod +x x
# ============================================================

.text
.globl _start

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

.org 48
    .int    1                            # [48:52] p_type  = PT_LOAD (= e_flags, ignored)
    .int    0x00380007                   # [52:56] p_flags = RWX; high half = e_phentsize = 56
    .quad   1                            # [56:64] p_offset = 1; low bytes = e_phnum = 1
    .quad   0x400001                     # [64:72] p_vaddr (== p_offset mod page)

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
.org 88
_start:
    leaq    apply(%rip), %rbp   # rbp = &apply
    ## Filler completing the build script's p_memsz window: the first 8
    ## .text bytes become [lea][00] whose LE value (~2.3 GB) is a valid
    ## p_memsz. 00 c9 = addb %cl,%cl, harmless here. (See x64.s.)
    .byte   0x00, 0xc9          # addb %cl, %cl
    .byte   0x8d, 0x5d          # leal disp8(%rbp), %ebx  (ModRM 5d: reg=ebx, base=rbp)
    .byte   leaf-apply          # rbx = leaf = heap base (disp8 = sizeof(apply))
    leal    8(%rbx), %edi       # rdi = heap free pointer (past the leaf node)

    ## Build Jay identity: fork(stem(leaf), stem(leaf)) — one shared stem.
    ## stem(leaf) = [leaf][0]; the v word stays 0 via BSS (scasl skips it).
    movl    %edi, %esi          # esi = stem addr S
    movl    %ebx, %eax          # eax = leaf
    stosl                       # S.u = leaf
    scasl                       # skip S.v (stays 0), rdi -> fork addr
    pushq   %rdi                # push fork addr (= result)
    xchg    %esi, %eax          # eax = S (esi = leaf)
    stosl                       # fork.u = S
    stosl                       # fork.v = S

1:  call    parse_tree
    popq    %rdx                # accumulator (pop before EOF test; pop leaves flags)
    js      2f
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  ## Fold loop done; retarget the dead rbp (= &apply) to emit_tree.
    .byte   0x8d, 0x6d          # leal disp8(%rbp), %ebp  (ModRM 6d: reg=ebp, base=rbp)
    .byte   emit_tree-apply     # disp8 = emit_tree - apply
    call    *%rbp               # emit_tree(edx = accumulator)
    movb    $60, %al            # SYS_EXIT
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
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al           # '0'->0, '1'->1, '2'->2, whitespace->negative
    js      .Lp_read            # skip non-digit
    movl    %eax, %ecx          # ecx = child count (0,1,2); eax stays 0/1/2 so
                                # scasq leaves SF clear (caller's EOF test is js)
    movl    %edi, %edx          # edx = node base
    pushq   %rdx
    scasq                       # reserve two words (u, v): rdi += 8 in 2 bytes
    jrcxz   .Lp_done            # count 0 -> leaf: the reserved [0][0] node is it
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    parse_tree
    popq    %rdx
    popq    %rcx
    movl    %eax, (%rdx)        # store child
    addl    $4, %edx            # next slot
    loop    .Lp_loop
.Lp_done:
    popq    %rax                # return base address
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless via the heap-base threshold (rbx).
    cmpl    %ebx, (%rdx)               # CF = (u == 0)
    sbbl    %ecx, %ecx                 # ecx = -(u == 0)
    cmpl    %ebx, 4(%rdx)              # CF = (v == 0)
    sbbl    $-2, %ecx                  # ecx = tag in {0,1,2} = child count
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
    movl    (%rdx), %edx               # child = *slot (offset 0 then 4)
    call    *%rbp                      # emit_tree (rbp retargeted to it in _start)
    popq    %rdx
    popq    %rcx
    addl    $4, %edx                   # next slot
    loop    .Lemit_loop
1:  ret

## ---- apply(edx=a, esi=b) -> eax ----  (placed last; see leaf note below)
apply:
    movl    (%rdx), %eax               # eax = a.u
    movl    4(%rdx), %ecx              # ecx = a.v
    jrcxz   .La_build                  # a.v == 0 -> a is leaf or stem

    movl    (%rax), %ecx               # u.u
    jrcxz   .Lu_leaf
    movl    4(%rax), %ecx              # u.v  (== x, kept in ecx for the fork rule)
    jrcxz   .Lu_stem
.Lu_fork:
    ## Jay rule 3 (F): fork(fork(w,x), y) · b = b·w·x  (y ignored).
    ## w = u.u, x = u.v (in ecx).
    pushq   %rcx                       # save x = u.v
    movl    %esi, %edx                 # a = b
    movl    (%rax), %esi               # b = w = u.u
    call    *%rbp                      # apply(b, w) -> eax
    popq    %rsi                       # b = x
    xchg    %eax, %edx                 # a = b·w
    jmp     *%rbp                      # tail apply(b·w, x)

.Lu_stem:
    ## Jay rule 2 (S): fork(stem(x), y) · b = (y·b)·(x·b).  x = u.u, y = a.v.
    pushq   %rdx                       # save a
    pushq   %rsi                       # save b
    movl    (%rax), %edx               # x = u.u
    call    *%rbp                      # apply(x, b) -> eax
    popq    %rsi                       # restore b
    popq    %rdx                       # restore a
    pushq   %rax                       # save x·b
    movl    4(%rdx), %edx              # y = a.v
    call    *%rbp                      # apply(y, b) -> eax
    xchg    %eax, %edx                 # a = y·b
    popq    %rsi                       # b = x·b
    jmp     *%rbp                      # tail apply(y·b, x·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].
    testl   %eax, %eax                 # a.u == 0 -> leaf
    jnz     1f
    xchg    %eax, %esi                 # leaf: eax=b, esi=0 (old a.u)
1:  pushq   %rdi
    stosl                              # write u = eax
    xchg    %eax, %esi                 # eax = v
    stosl                              # write v
    popq    %rax
    ret

## Canonical leaf at end of .text ([0][0] via BSS zero-fill). apply must
## stay within 127 bytes of leaf (disp8 for leaf-apply / emit_tree-apply).
leaf:
.Lend:
