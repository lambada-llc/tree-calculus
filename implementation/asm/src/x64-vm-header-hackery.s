# ============================================================
# Hand-crafted, *Linux-valid* ELF (tagless two-word nodes, continuation-stack apply).
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
#   as x64-vm-header-hackery.s -o x.o && ld -Ttext=0x400000 x.o -o x.elf
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
    leaq    leaf(%rip), %rbx    # rbx = leaf address = heap base ([0][0] from BSS);
                                # leaf (end of .text) keeps the lea disp small — the
                                # page-aligned .bss symbol would blow up the window value
    ## Filler completing the build script's p_memsz window: the first 8
    ## .text bytes become [lea][00] whose LE value (~5 GB) is a valid
    ## p_memsz. 00 c9 = addb %cl,%cl, harmless here. (See x64.s.)
    .byte   0x00, 0xc9          # addb %cl, %cl
    leal    8(%rbx), %edi       # rdi = free pointer, past the leaf node
    movl    $parse_tree, %ebp   # 5B absolute (ld -Ttext resolves it); parse
                                # is called twice via the 2-byte call *%rbp

    ## Build identity: fork(fork(leaf, leaf), leaf) — two-word forks.
    movl    %ebx, %eax          # eax = leaf
    movl    %edi, %esi          # esi = inner fork addr
    stosl                       # inner.u = leaf
    stosl                       # inner.v = leaf
    pushq   %rdi                # push outer fork addr (= result)
    xchg    %esi, %eax          # eax = inner
    stosl                       # outer.u = inner
    xchg    %esi, %eax          # eax = leaf
    stosl                       # outer.v = leaf

1:  call    *%rbp               # parse_tree
    popq    %rdx                # accumulator (pop before EOF test; pop leaves flags)
    js      2f
    xchg    %eax, %esi          # 1 byte instead of 2
    call    apply               # apply(edx, esi) -> eax
    pushq   %rax
    jmp     1b

2:  call    emit_tree
    jmp     .Lexit              # exit epilogue lives in p_paddr


## ---- apply(edx=a, esi=b) -> eax ----
## Non-recursive VM: an explicit continuation stack on the machine stack.
## Frame tags:  -1 = sentinel (bottom), 0 = APPLY_TO(arg), 1 = COMPUTE_AND_APPLY(fn,arg).
## These frame tags are on the machine stack and are unrelated to the
## tagless node layout below.
apply:
    pushq   $-1                 # sentinel: marks bottom of continuation stack
.Lreduce:
    movl    (%rdx), %eax        # a.u
    movl    4(%rdx), %ecx       # a.v
    jrcxz   .Lvm_a_build        # a.v == 0 -> a is leaf or stem

    ## a = fork(u, y): u = a.u (eax). Classify u.
    movl    (%rax), %ecx        # u.u
    jrcxz   .Lvm_u_leaf
    movl    4(%rax), %ecx       # u.v
    jrcxz   .Lvm_u_stem

    ## u = fork(w, x): triage on b.
    movl    (%rsi), %ecx        # b.u
    jrcxz   .Lvm_b_leaf
    movl    4(%rsi), %ecx       # b.v
    jrcxz   .Lvm_b_stem

    ## b = fork(d, e): apply(apply(y, d), e).
    ## Load e, y, d; push APPLY_TO(e) and reduce apply(y, d).
    movl    4(%rsi), %eax       # e = b.v
    movl    4(%rdx), %edx       # y = a.v
    movl    (%rsi), %esi        # d = b.u
.Lpush_at_reduce:
    pushq   %rax                # push e (or result in CAA case)
    pushq   $0                  # tag = APPLY_TO
    jmp     .Lreduce

.Lvm_u_stem:
    ## u = stem(u'): apply(apply(u', b), apply(y, b)).
    ## Push COMPUTE_AND_APPLY(u', b), reduce apply(y, b).
    movl    (%rax), %eax        # u' = u.u
    pushq   %rsi                # arg2 = b
    pushq   %rax                # arg1 = u'
    pushq   $1                  # tag = COMPUTE_AND_APPLY
    movl    4(%rdx), %edx       # a = y = a.v
    jmp     .Lreduce

.Lvm_b_stem:
    ## b = stem(d): apply(x, d).  x = u.v, d = b.u
    movl    4(%rax), %edx       # a = x = u.v
    movl    (%rsi), %esi        # b = d = b.u
    jmp     .Lreduce

.Lvm_u_leaf:
    ## apply(fork(leaf, y), b) = y = a.v
    movl    4(%rdx), %eax
    jmp     .Ldispatch

.Lvm_b_leaf:
    ## b = leaf: result = w = u.u
    movl    (%rax), %eax
    ## fall through to .Ldispatch

.Ldispatch:
    popq    %rcx                # frame tag: -1=sentinel, 0=APPLY_TO, 1=CAA
    jrcxz   .Lvm_at             # 0 -> APPLY_TO
    incl    %ecx
    jz      .Lvm_done           # -1 -> sentinel, done

    ## COMPUTE_AND_APPLY(fn, arg): push APPLY_TO(result), reduce apply(fn, arg)
    popq    %rdx                # fn -> a
    popq    %rsi                # arg -> b
    jmp     .Lpush_at_reduce

.Lvm_at:
    ## APPLY_TO: a = result, b = arg
    popq    %rsi                # b = arg
    xchg    %eax, %edx          # a = result
    jmp     .Lreduce

.Lvm_done:
    ret

.Lvm_a_build:
    ## a=leaf -> stem(b)=[b][0]; a=stem(x) -> fork(x,b)=[x][b].  Then dispatch.
    testl   %eax, %eax          # a.u == 0 -> leaf
    jnz     1f
    xchg    %eax, %esi          # leaf: eax=b, esi=0
1:  pushq   %rdi
    stosl                       # write u
    xchg    %eax, %esi
    stosl                       # write v
    popq    %rax                # result
    jmp     .Ldispatch

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
    scasq                       # reserve two words (u, v)
    jrcxz   .Lp_done            # count 0 -> leaf: the reserved [0][0] node is it
.Lp_loop:
    pushq   %rcx
    pushq   %rdx
    call    *%rbp               # parse_tree
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
    cmpl    %ebx, (%rdx)
    sbbl    %ecx, %ecx
    cmpl    %ebx, 4(%rdx)
    sbbl    $-2, %ecx
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
    movl    (%rdx), %edx        # child = *slot (offset 0 then 4)
    call    emit_tree
    popq    %rdx
    popq    %rcx
    addl    $4, %edx            # next slot
    loop    .Lemit_loop
1:  ret

leaf:
.Lend:
