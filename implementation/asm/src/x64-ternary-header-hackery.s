# ============================================================
# Hand-crafted, *Linux-valid* ELF (the original tagged-ternary representation).
#
# Every field the Linux kernel actually validates holds its required value,
# so this binary execs on a stock kernel:
#
#   - phdr at offset 48, overlapping the ehdr tail. The kernel-read fields
#     there are e_phentsize (=56, doubled by p_flags' high half; the loader
#     only inspects the PF_R/W/X bits) and e_phnum (=1, doubled by
#     p_offset=1's low bytes). e_flags/e_ehsize/e_sh* absorb the rest.
#   - p_offset=1 with p_vaddr=0x400001: file offset F maps to 0x400000+F,
#
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
#   as x64-ternary-header-hackery.s -o x.o && ld -Ttext=0x400000 x.o -o x.elf
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
    leaq    .Lend(%rip), %rbx    # rbx = heap base = leaf address
    ## Filler: the first 8 .text bytes become [lea][00], whose LE value
    ## (~2.2 GB) doubles as p_memsz. 00 c9 = addb %cl,%cl, harmless here.
    .byte   0x00, 0xc9          # addb %cl, %cl
    leal    8(%rbx), %edi       # rdi = heap free pointer

    ## Build identity: fork(fork(leaf, leaf), leaf) — inlined
    push    $2
    pop     %rax                # eax = 2
    movl    %edi, %ebp          # ebp = inner fork addr
    stosl                       # inner.tag = 2
    xchg    %ebx, %eax          # eax = leaf, ebx = 2 (temp)
    stosl                       # inner.left = leaf
    stosl                       # inner.right = leaf
    pushq   %rdi                # push outer fork addr (= result)
    xchg    %ebx, %eax          # eax = 2, ebx = leaf (restored)
    stosl                       # outer.tag = 2
    xchg    %ebp, %eax          # eax = inner fork addr
    stosl                       # outer.left = inner
    movl    %ebx, %eax
    stosl                       # outer.right = leaf
    movl    $apply, %ebp        # rbp = &apply (5B absolute: ld -Ttext resolves it)

1:  call    parse_tree
    js      2f
    popq    %rdx
    xchg    %eax, %esi          # 1 byte instead of 2
    call    *%rbp               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  popq    %rdx
    call    emit_tree
    jmp     .Lexit              # exit epilogue lives in p_paddr


## ---- apply(edx=a, esi=b) -> eax ----
apply:
    movl    (%rdx), %ecx
    cmpl    $2, %ecx
    jae     .La_fork

    ## a=leaf (ecx=0) or a=stem (ecx=1): build [tag+1, ...a.children, b]
    pushq   %rdi                       # save result addr
    leal    1(%rcx), %eax              # tag = a.tag + 1
    stosl                              # write tag
    jrcxz   1f                         # leaf: no children to copy
    movl    4(%rdx), %eax              # a.child (stem case)
    stosl                              # write it
1:  xchg    %esi, %eax                 # eax = b
    stosl                              # append b
    popq    %rax                       # result = start of node
    ret

.La_fork:
    movl    4(%rdx), %eax              # u (eax = u addr)
    movl    (%rax), %ecx               # u.tag
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
    pushq   %rdx                       # save a       [a]
    pushq   %rsi                       # save b       [b][a]
    movl    4(%rax), %edx              # x = u.child
    call    *%rbp                      # apply(x, b) -> eax
    popq    %rsi                       # restore b
    popq    %rdx                       # restore a
    pushq   %rax                       # save x·b     [x·b]
    movl    8(%rdx), %edx              # y = a.right
    call    *%rbp                      # apply(y, b) -> eax
    xchg    %eax, %esi                 # esi = y·b (1B)
    popq    %rdx                       # edx = x·b
    jmp     apply                      # tail call apply(x·b, y·b)

.Lu_leaf:
    ## rule 1: a.right
    movl    8(%rdx), %eax
    ret

## (alloc_fork/alloc_stem removed — unified into apply body + inlined _start)

## ---- parse_tree -> eax (SF set on EOF) ----
## Read byte via do_io. If 0, return leaf. On EOF, return with SF set.
## Otherwise: stosl tag, pre-bump rdi, recurse d times storing children.
parse_tree:
.Lp_read:
    xorl    %eax, %eax          # eax=0=SYS_READ
    call    do_io
    decl    %eax                # 1 → 0 (byte read), else → eof
    jnz     .Lp_ret
    movb    %cl, %al
    subb    $'0', %al           # ZF if '0', SF if < '0'
    jz      .Lp_leaf             # leaf: return heap base
    js      .Lp_read            # skip non-digit
    movl    %edi, %edx
    stosl                       # store tag
    pushq   %rdx
    leaq    (%rdi,%rax,4), %rdi # pre-bump free pointer past children
    xchg    %eax, %ecx          # ecx = loop counter
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    parse_tree
    popq    %rdx
    popq    %rcx
    addl    $4, %edx
    movl    %eax, (%rdx)
    loop    .Lp_loop
    popq    %rax                # return base address
    ret
.Lp_leaf:
    movl    %ebx, %eax          # leaf = heap base
.Lp_ret:
    ret

## ---- emit_tree(edx=tree) — recursive, byte-at-a-time output ----
emit_tree:
    movl    (%rdx), %ecx
    pushq   %rcx
    pushq   %rdx
    addb    $'0', %cl
    call    write_byte
    popq    %rdx
    popq    %rcx
    jrcxz   1f
.Lemit_loop:
    addl    $4, %edx
    pushq   %rcx
    pushq   %rdx
    movl    (%rdx), %edx
    call    emit_tree
    popq    %rdx
    popq    %rcx
    loop    .Lemit_loop
1:  ret
.Lend:
