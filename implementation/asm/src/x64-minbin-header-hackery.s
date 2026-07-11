# ============================================================
# Hand-crafted, *Linux-valid* ELF (tagless two-word nodes, minbin I/O).
#
# Same phdr-at-48 layout as x64-header-hackery.s.
# Code lives in every hole the kernel ignores. This build is NOT Rosetta-
# compatible: Rosetta validates e_ident[4:16], which here is executable
# code (see x64-rosetta for the compatible twin).
#   e_ident  [4:16]  — do_io argument setup + syscall/cleanup tail
#   e_shoff  [40:48] — write_byte + do_io head (jmp back into e_ident)
#   p_paddr  [72:80] — the exit epilogue (jmp .Lexit from the stream)
#   p_memsz  [88:96] — _start's first 8 bytes: lea+00, whose LE value
#                      (~2.2 GB) doubles as a valid "big enough" memsz
#   p_align  [96:..] — ignored for ET_EXEC; code flows contiguously from 96
#
# Build:
#   as x64-minbin-header-hackery.s -o x.o && ld -Ttext=0x400000 x.o -o x.elf
#   objcopy -O binary -j .text x.elf x && chmod +x x
# ============================================================

.text
.globl _start

# ================================================================
# ELF64 Header — non-Rosetta layout: Linux only checks the 4 magic
# bytes of e_ident, so [4:16] is 12 bytes of code (Rosetta validates
# class/data/version/pad there; the x64-rosetta variant keeps them).
# ================================================================
ehdr:
    .byte   0x7f, 'E', 'L', 'F'          # [0:4]   magic — all Linux reads of e_ident

# ---- e_ident[4:16] (kernel-ignored): do_io argument setup + tail ----
.Ldo_io2:
    push    %rcx                         # byte on stack (write: cl=data; read: overwritten)
    push    %rsp
    pop     %rsi                         # buffer = stack
    push    $1
    pop     %rdx                         # count = 1
    syscall
    pop     %rcx
    popq    %rdi                         # restore free pointer
    ret

.org 16
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
    jmp     .Ldo_io2                     # rel8 back into e_ident[4:16]

# ================================================================
# Program Header (offset 48, overlapping ehdr[48:64])
# ================================================================
.org 48
    .int    1                            # [48:52] p_type  = PT_LOAD (= e_flags, ignored)
    .int    0x00380007                   # [52:56] p_flags = RWX; high half = e_phentsize = 56
    .quad   1                            # [56:64] p_offset = 1; low bytes = e_phnum = 1
    .quad   0x400001                     # [64:72] p_vaddr (== p_offset mod page)

# ---- p_paddr [72:80] (kernel-ignored): exit epilogue ----
.Lexit:
    movb    $60, %al                     # SYS_EXIT (rax upper bytes 0 from last write)
    xorl    %edi, %edi                   # status = 0
    syscall

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
    leal    8(%rbx), %edi       # rdi = free pointer, skip leaf node

    call    parse_eval          # parse + eval entire stdin -> eax
    xchg    %eax, %edx          # edx = result (1 byte vs 2)
    ## parse/eval done; retarget the dead rbp (= &apply) to emit_tree.
    .byte   0x8d, 0x6d          # leal disp8(%rbp), %ebp  (ModRM 6d: reg=ebp, base=rbp)
    .byte   emit_tree-apply     # disp8 = emit_tree - apply
    call    *%rbp               # emit_tree(edx = result)
    jmp     .Lexit              # exit epilogue lives in p_paddr



## ---- parse_eval -> eax (tree pointer) ----
## '1' -> leaf ; '0' -> apply(parse_eval, parse_eval) ; EOF -> leaf.
parse_eval:
.Lpe_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lpe_leaf           # EOF: return leaf
    subb    $'0', %cl           # cl = char - '0'; CF if < '0'
    jb      .Lpe_read           # < '0': skip whitespace
    je      .Lpe_apply          # '0': application
    decb    %cl                 # was '1'?
    jnz     .Lpe_read           # > '1': skip
.Lpe_leaf:
    movl    %ebx, %eax          # leaf = heap base
    ret
.Lpe_apply:
    call    parse_eval          # a = first subexpr
    pushq   %rax
    call    parse_eval          # b = second subexpr
    xchg    %eax, %esi          # esi = b
    popq    %rdx                # edx = a
    jmp     *%rbp               # tail apply(a, b)   (rbp = &apply here)

## ---- emit_tree(edx=tree) -> minbin on stdout ----
## leaf -> '1' ; stem(c) -> '0' '1' <c> ; fork(l,r) -> '0' '0' '1' <l> <r>.
## i.e. emit (tag) zeros, then '1', then recurse on children.
emit_tree:
    ## tag = 2 - (u==0) - (v==0), branchless via the heap-base threshold (rbx).
    cmpl    %ebx, (%rdx)
    sbbl    %ecx, %ecx
    cmpl    %ebx, 4(%rdx)
    sbbl    $-2, %ecx                  # ecx = tag in {0,1,2}
    pushq   %rdx                       # save node for the function lifetime
    jrcxz   .Le_one                    # tag 0 (leaf): no leading zeros
    pushq   %rcx
.Le_zero_loop:
    pushq   %rcx
    movb    $'0', %cl
    call    write_byte
    popq    %rcx
    loop    .Le_zero_loop
    popq    %rcx
.Le_one:
    pushq   %rcx
    movb    $'1', %cl
    call    write_byte
    popq    %rcx
    jrcxz   .Le_done                   # leaf: no children
    popq    %rdx                       # restore node
    decl    %ecx                       # tag-1
    pushq   %rcx
    pushq   %rdx
    movl    (%rdx), %edx               # first child (offset 0)
    call    *%rbp                      # emit_tree (rbp retargeted to it)
    popq    %rdx
    popq    %rcx
    jrcxz   1f                         # stem: done after one child
    movl    4(%rdx), %edx              # fork right child (offset 4)
    jmp     *%rbp                      # tail emit_tree
.Le_done:
    popq    %rdx                       # clean up entry push
1:  ret

## ---- apply(edx=a, esi=b) -> eax ----  (placed last; see leaf note below)
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
    movl    (%rax), %edx               # x = u.u
    call    *%rbp                      # apply(x, b)
    popq    %rsi
    popq    %rdx
    pushq   %rax                       # save x·b
    movl    4(%rdx), %edx              # y = a.v
    call    *%rbp                      # apply(y, b)
    xchg    %eax, %esi                 # esi = y·b
    popq    %rdx                       # edx = x·b
    jmp     *%rbp                      # tail apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.v
    movl    4(%rdx), %eax
    ret

.La_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].
    testl   %eax, %eax
    jnz     1f
    xchg    %eax, %esi                 # leaf: eax=b, esi=0
1:  pushq   %rdi
    stosl                              # write u
    xchg    %eax, %esi
    stosl                              # write v
    popq    %rax
    ret

## Canonical leaf at end of .text ([0][0] via BSS zero-fill). apply must
## stay within 127 bytes of leaf (disp8 for leaf-apply / emit_tree-apply).
leaf:
.Lend:
