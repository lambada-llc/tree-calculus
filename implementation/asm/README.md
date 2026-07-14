# Size-Golfed Tree Calculus Evaluators — x86 Assymbly

The smallest working ELF binaries that evaluate [tree calculus](https://treecalcul.us/specification/).

A huge thank you to Justine Tunney, her [Lambda Calculus in 383 Bytes](https://justine.lol/lambda/) post was a big source of inspiration for this code golfing exercise. Note: The smallest binaries here are closer to 300 bytes, which is exciting, but I wanna emphasize that those evaluators **don't implement garbage collection** while Justine's record program does! So this is not an apple-to-apple comparison between tree calculus and lambda calculus. At least for now.

## Variants

The variants differ along three axes:

### By I/O format

**Ternary** (`x64`, `x64-rosetta`, `x64-ternary`, `x64-jay`, `x64-noid`, `x64-vm`): Reads one ternary-encoded tree per stdin line (`0`=leaf, `1X`=stem, `2XY`=fork). Left-folds application across all inputs (starting from the identity tree). Writes the result to stdout in the same encoding.

```sh
# 21100 is the identity tree; applying it to 10 returns 10
{ echo 21100; echo 10; } | bin/x64

# "true" (λa.λb.a) has encoding "10":
{ echo 10; echo 0; echo 200; } | bin/x64   # → 0
{ echo 10; echo 200; echo 0; } | bin/x64   # → 200

# "false" (λa.λb.b) has encoding "2021100":
{ echo 2021100; echo 0; echo 200; } | bin/x64   # → 200
```

**Minbin** (`x64-minbin`, `x64-minbin-deep`): Reads a single minimalist binary expression from stdin (`1`=leaf, `0AB`=apply(A,B), one ASCII character per bit). Application is encoded directly in the input — no left-fold. Writes the result in minbin.

```sh
# Identity applied to leaf:
echo '0 001010111 1' | bin/x64-minbin         # → 1

# Identity applied to stem(leaf):
echo '0 001010111 011' | bin/x64-minbin      # → 011
```

### By reduction rules

There is actually not just one tree calculus, it is a family of calculi. See [Barry Jay's post](https://github.com/barry-jay-personal/blog/blob/main/2024-12-12-calculus-calculi.md). Two variants explored here:

**Triage calculus** (all except `x64-jay`): Five reduction rules, as specified at [treecalcul.us](https://treecalcul.us/specification/). Note: This is the calculus this entire repo focuses on. As the name suggests, triage calculus has triaging "baked into" the rules (rules 3a-c), which makes triaging cheap both in terms of nodes count and reduction steps. However, for a smallest evaluator, it helps to have fewer rules:

**Jay** (`x64-jay`): Barry Jay's original three reduction rules from [*Reflective Programs in Tree Calculus*](https://github.com/barry-jay-personal/tree-calculus/blob/master/tree_book.pdf).

### By internal representation

**Two-word tagless nodes** (default — all except `x64-ternary` and `x64-minbin-deep`): Every node is exactly two i32 words, `[u][v]` (8 bytes), with the shape implicit in whether the child pointers are null:

- `u == 0` → **leaf** (`v` ignored; the canonical leaf is `[0][0]` at the heap base)
- `u != 0, v == 0` → **stem**(`u`)
- `u != 0, v != 0` → **fork**(`u`, `v`)

Heap addresses are always non-zero, so `0` unambiguously means "no child" — there is no tag word. Forks shrink from 12 to 8 bytes, and construction is just a pair of stores with no tag write. `apply`'s leaf/stem/fork and triage cases fall out of `jrcxz` null-checks with `jmp *%rbp` tail calls (no per-child loop, no tag arithmetic), and the ternary tag (0/1/2) is only reconstructed where genuinely needed — in `emit` — branchlessly. Because every non-null child is `>=` the heap base, any code address is a valid null-threshold — during emit `rbp` points at `emit_tree`, so `cmpl %ebp, word` sets the carry flag exactly when `word == 0` (no dedicated leaf register needed), so the tag is `sbb %ecx,%ecx; sbb $-2,%ecx` = `2 - (u==0) - (v==0)` in four instructions. Net effect vs. the tagged layout: ~15–20% faster on reduction-heavy workloads *and* smaller across the board — the tighter nodes, branchless dispatch, and tail-call reduction outweigh `emit` rebuilding the tag. It is the default across the family; only the two variants whose whole point is a different layout keep theirs.

**Tagged-ternary nodes** (`x64-ternary`): the original `x64` layout, preserved for comparison — `[tag:32][child1:32][child2:32]` with tag 0/1/2 for leaf/stem/fork (4/8/12 bytes). Same reduction rules and I/O as `x64`; only the node representation differs (343 B vs. 317 B).

**Deep app-trees** (`x64-minbin-deep`): Only two node types — `leaf [tag=0]` and `app [tag=1][left][right]`. Ternary forms are nested apps: `stem(x)` = `app(leaf,x)`, `fork(x,y)` = `app(app(leaf,x),y)`. Simpler allocation and emission; deeper pattern matching in `apply()`. This layout *is* the point of the variant, so it keeps its tagged app-nodes rather than switching to the two-word form.

### Other differences

**x64-rosetta**: the Rosetta-compatible twin of `x64`. Its header-hackery build keeps `e_ident[4:16]` canonical and zero, which [Rosetta](https://support.apple.com/en-us/102527) validates but Linux ignores — every *other* variant's header-hackery build uses those 12 bytes as code and therefore loads only on a real Linux kernel (worth 9 bytes each). The standard (toolchain) builds never touch `e_ident`, so there `x64-rosetta` and `x64` are byte-identical.

**x64-ternary**: `x64` with the original tagged-ternary node layout instead of the two-word tagless one (see *By internal representation*). Kept as the head-to-head baseline for the representation change.

**x64-noid**: Omits the identity tree builder at startup. The first input becomes the accumulator directly instead of being applied to the identity. Undefined behavior for fewer than 2 inputs. Saves ~11 bytes.

## Sizes

See `sizes.txt`, generated by `build.sh`.

## Build & Test

```sh
./build.sh              # build all variants, both strategies
./build.sh x64 x64-jay  # build specific variants
./build.sh --clean      # remove all built binaries
./build.sh --sizes      # print size table
./build.sh --test       # run tests (delegates to node test.mjs)
node test.mjs           # run all tests directly
node test.mjs x64       # test specific variants
```

Requires: `gcc`, `strip`, `readelf`, `awk`, `python3`, `as`, `ld`, `objcopy`.

## Tricks

A collection of techniques used across the variants.

Again I want to thank Justine Tunney! I have little experience in machine code golfing, learnt a lot of tricks from [Lambda Calculus in 383 Bytes](https://justine.lol/lambda/) and bet that she'd find more bytes to squeeze out of these implementations.

Disclaimer: These binaries were originally developed on [Apple silicon via Rosetta](https://support.apple.com/en-us/102527), which prevents _some_ cursed ELF header tricks — most notably it validates `e_ident[4:16]`, 12 bytes Linux never reads. Only `x64-rosetta` still honors that constraint; the other header-hackery builds execute code out of those bytes (worth 9 bytes each). Every build loads on a stock Linux kernel, so the whole family is covered by the test suite.

### ELF structure

**No libc, no dynamic linking.** `gcc -nostdlib -static` with `_start` as the entry point. The only syscalls are `read`, `write`, and `exit`.

**BSS heap via `p_memsz > p_filesz`.** The ELF spec says the kernel zero-fills the difference. Making `p_memsz` span a couple of GB past the file gives us a zero-initialized heap with no `mmap` or `brk` syscall. The heap starts at the first byte after the code (a link-time constant). We never free — it's a bump allocator in `rdi`.

**NMAGIC linking (`-Wl,-n`).** Suppresses page-alignment padding between ELF headers and code. Without this, the toolchain inserts ~3.7 KB of zeros to align `.text` to a page boundary.

**Section header removal.** After linking and stripping, the post-processor rebuilds the file without section headers (`e_shoff`/`e_shnum`/`e_shstrndx` = 0) and truncates it right after `.text` ends. The kernel ignores section headers entirely — they're only used by debuggers.

**Phdr-at-48 overlap + code-in-phdr (standard build).** A Python post-processing script rebuilds the toolchain ELF with the program header at offset 48, overlapping the ELF header's tail. The only ehdr fields the kernel still reads in [48:64] are `e_phentsize` (=56, doubled by `p_flags`' high half — the kernel only looks at the low PF_R/W/X bits) and `e_phnum` (=1, doubled by `p_offset=1`'s low bytes). The script then stores the first 16 bytes of `.text` inside the phdr itself: `p_align` [96:104] is ignored for `ET_EXEC`, and `p_memsz` [88:96] only has to be "big enough but mappable" — sources start with `lea` + a 2-byte `addb %cl,%cl` filler so their first 8 bytes end in `00 00`, reading as a ~2.3 GB memsz (the earlier 512 MB heap now rides along for free; a 64-bit value assembled from arbitrary code bytes would exceed the user address-space limit and make `exec` fail, which is why the filler matters). Headers shrink from 120 to 88 bytes — and unlike the header-hackery ELFs (whose `e_phentsize=0` only loads under Rosetta), these still exec on a stock Linux kernel. Sources whose leading bytes don't form a valid memsz automatically fall back to code-at-96 (headers cost 96 bytes).

**Phdr-at-48 overlap (header-hackery build).** The hand-crafted ELF starts the program header at offset 48, overlapping the last 16 bytes of the ELF header. Fields are chosen so both interpretations are valid — crucially including the two the kernel actually checks: `p_flags`' high half doubles as `e_phentsize=56` and `p_offset=1`'s low bytes double as `e_phnum=1`. (The previous layout put the phdr at 40 with `e_phentsize=0`, which only Rosetta's lax loader accepted; these binaries now exec on a stock Linux kernel and are covered by the test suite.) Offset 40 is impossible on real Linux: there `p_offset` would overlap `e_phentsize`, forcing a ~2^51 file offset.

**Code in don't-care header fields.** The header-hackery builds weave executable instructions into ELF/Phdr fields the kernel doesn't validate. Linux reads only the 4 magic bytes of `e_ident`, so in the non-Rosetta variants the whole I/O stub and the exit epilogue live inside the headers — zero I/O bytes remain in the main stream:

- `e_ident` [4:16] — `do_io`'s argument setup plus its `syscall`/cleanup/`ret` tail (11 of 12 bytes). Rosetta validates these bytes, so `x64-rosetta` forgoes this hole (costing it 9 bytes).
- `e_shoff` [40:48] — `write_byte` + the head of `do_io` (8 bytes exactly), ending in a rel8 `jmp` *backwards* into `e_ident`. Calls land here directly — a function head in an island needs no entry glue.
- `p_paddr` [72:80] — the exit epilogue (`mov $60,%al; xor %edi,%edi; syscall`); the stream reaches it with a 2-byte `jmp` (every variant's fold loop ends within rel8 range of offset 72).
- `p_memsz` [88:96] — `_start`'s first 8 bytes: a 7-byte `lea` whose disp32 high bytes are `00 00 00`, plus a filler `00` closing the window. Read as a number this is ~2.2 GB — big enough for the heap, small enough to map (an arbitrary 8-byte code window would exceed the user address-space limit and abort `exec`).
- `p_align` [96:104] — ignored for `ET_EXEC`; the rest of the code simply flows from offset 96 with no boundary at all.

`x64-minbin-deep` additionally places `emit_tree` early in the stream so its leaf case can tail-call `write_byte` in the `e_shoff` island with a 2-byte `jmp` (the island is within rel8 range only from the first ~170 bytes).

**`p_offset=1` for Rosetta compatibility.** (Used by both builds.) macOS's Rosetta requires `p_offset % page_size == p_vaddr % page_size`. Setting `p_offset=1` (with `p_vaddr=0x400001`) satisfies this while `e_phnum=1` is encoded in the low 2 bytes of `p_vaddr`.


### x86 instruction selection

**`xchg %eax, %reg`** — 1 byte (opcodes 0x90–0x97) vs 2 bytes for `movl %reg, %eax`. Only the `%eax`-form has the short encoding; other register pairs are 2 bytes. Extremely useful for shuffling arguments through `%eax`.

**`push $imm8; pop %reg`** — 3 bytes to load any small constant (-128 to 127) into any register. Often shorter than `movl $imm, %reg` (5 bytes) or `movb $imm, %al` (2 bytes, but only sets the low byte).

**`cdq`** — 1 byte, sign-extends `%eax` into `%edx`. When `%eax` is a small positive value, this zeroes `%edx` for free vs 2-byte `xorl %edx, %edx`.

**`stosl` / `stosb`** — 1-byte sequential heap writes. `stosl` writes `%eax` to `[%rdi]` and advances `%rdi` by 4. Ideal for building nodes linearly on the heap. The alloc functions are essentially `stosl` sequences.

**`lodsl`** — 1 byte, reads `[%rsi]` into `%eax` and bumps `%rsi += 4`. Replaces a `movl (%reg), %eax` (2-3B) + `addl $4, %reg` (3B) pair. Used in the triage dispatch loop to iterate over a node's children.

**`jrcxz` / `jecxz`** — 2-byte zero-test on `%rcx`/`%ecx` without needing flags. Used for EOF detection (`read` returns 0 bytes) and tag dispatch.

**`loop`** — 2 bytes for `decl %ecx; jnz target`, replacing a 3-4 byte explicit sequence.

**`leal` instead of `leaq`** — in 64-bit mode, `leal 8(%rbx),%edi` is 3 bytes (no REX.W prefix); `leaq 8(%rbx),%rdi` is 4 bytes. Works when addresses fit in 32 bits (loaded at 0x400000, upper bits auto-zeroed).

**`addb $imm8, %cl`** — 3 bytes for character conversion (tag → ASCII). Replaces `leal '0'(%rcx),%eax` (3B) + `movb %al,%cl` (2B) = 5 bytes.

**`subb` for multi-way dispatch** — `subb $'0', %cl` sets CF for chars below `'0'`, ZF for `'0'` itself, and leaves `%cl = original - '0'`. A subsequent `decb %cl; jnz` tests for `'1'`. Three conditions tested in 7 bytes instead of 14 bytes of compare-and-branch chains.

**`btsl $26, %esi`** — 4 bytes to produce the value 0x4000000 (64 MB) for `p_memsz`. Shorter than loading the constant directly.

### Register allocation

**Rely on kernel-zeroed registers.** Linux zeroes all GPRs before entering `_start`. This means no initialization is needed for `%rdi` (heap free pointer starts at 0), and `mov $imm8, %al` (2B) works instead of `mov $imm32, %eax` (5B) when the upper bytes are known to be zero.

**`call *%rbp` (register-indirect call)** — `call rel32` is always 5 bytes on x86. By dedicating `%rbp` to hold `&apply` at init, each call site becomes `call *%rbp` = 2 bytes, saving 3 bytes per site. Init cost is 5-7 bytes; net savings are 5-7 bytes for 4-call variants, 2-4 bytes for 3-call variants.

**`%eax` for the `u` pointer in `apply`** — x86 ModR/M encoding can't represent `(%rbp)` at mod=00 (reserved for RIP-relative addressing), so the assembler emits mod=01 + `disp8=0x00` — 3 bytes total. `(%rax)` at mod=00 is only 2 bytes. Switching the fork's `u` pointer from `%ebp` to `%eax` saves 1 byte per zero-displacement access.

**Avoid 0x67 address-size override prefix** — using a 32-bit register as an address base in 64-bit mode (e.g., `(%ebp)`) adds a 1-byte prefix. Use 64-bit register forms (`(%rbp)`) to avoid this.

**Retarget `%rbp` after the fold loop (`x64`).** `%rbp` holds `&apply` for the reduction loop, but once EOF ends the loop nothing calls `apply` again, so the epilogue points `%rbp` at `emit_tree` instead. The retarget `lea` is paid for by the top-level emit call (`call rel32` 5B → `lea` 3B + `call *%rbp` 2B), and `emit`'s self-recursion then drops from 5 bytes to 2. Net −3.

### Tagless two-word representation (`x64`)

The tagless `[u][v]` layout (see *By internal representation*) enables a few tricks of its own:

**Heap base as a null threshold.** Every non-null child pointer is `>=` the leaf/heap-base address kept in `%rbx`, and `0` is the only value below it. So `cmpl %ebx, word` sets CF exactly when `word == 0` — a 2-byte null test (3 bytes at `disp8`) that is shorter than `cmpl $1, word` (3/4 bytes) and needs no scratch register.

**Reconstruct the ternary tag with two `sbb`s.** `emit` needs `tag = 2 - (u==0) - (v==0)`. Using the threshold test above: `sbb %ecx,%ecx` turns the first carry into `-(u==0)` (0 or −1), then `sbb $-2,%ecx` computes `ecx + 2 - CF` for the second — yielding `{0,1,2}` in four instructions, no `push $2` and no immediate compare.

**`scasq` to reserve a node.** A node is always two i32 words, so `scasq` (2 bytes) bumps the bump-allocator `%rdi` by 8 — one byte shorter than `addl $8,%edi`. (It also does a dead compare against `[rdi]`, whose flags are irrelevant.)

**A leaf is just a count-0 node.** Because *any* `u==0` node is a leaf, parsing `'0'` needs no special case: it flows through the normal allocation path with child count 0, and a `jrcxz` skips the fill loop over the freshly `scasq`-reserved (BSS-zero) `[0][0]` node.

**Reuse the classification load.** In `apply`'s triage, the fork case wants `b`'s right child `q`, which the immediately-preceding `movl 4(%rsi),%ecx; jrcxz` already left in `%ecx` — so `pushq %rcx` replaces a reload.

**Hand-encode a `disp8` past a forward reference.** `_start` derives the leaf/heap base as `apply + sizeof(apply)`, and retargets `%rbp` to `emit_tree` as `apply + (emit_tree-apply)`. Both offsets fit a signed byte, but as forward references the assembler picks the 6-byte `disp32` `lea`. Emitting the opcode + ModRM + `.byte (label-label)` by hand forces the 3-byte `disp8` form; the displacement stays a same-section constant, so it survives the phdr-overlap file shift and needs no relocation.

### Absolute addressing (no SIB bytes)

Heap base `.Lend` is a link-time constant, and all addresses fit in 32 bits. Node pointers are stored as absolute addresses, so every heap access like `movl 4(%rbx,%rdx),%ecx` (4 bytes: opcode + ModR/M + SIB + disp8) becomes `movl 4(%rdx),%ecx` (3 bytes: no SIB). With ~14 heap accesses in `apply` alone, this saves 13+ bytes per variant.

**32-bit is the sweet spot for pointer width.** Every variant already stores node fields as i32 (`stosl`/`movl`; there is no `stosq`/`movq` heap store anywhere), so there is no 64→32 narrowing left to reclaim. Going *below* 32 bits cannot shrink the file and would actually grow it: the heap lives in BSS (`.lcomm`), so node width contributes zero file bytes no matter what — only `.text` counts — and 16-bit accesses (`movw`/`stosw`) each carry a `0x66` operand-size prefix, making every heap load/store one byte longer. Worse, child pointers are absolute addresses at/above `0x400000` spanning a 512 MB heap (intrinsically ~30 bits), which both the null-vs-pointer discrimination and the SIB-free addressing above rely on; 16-bit fields would force base-relative indices and bring the SIB bytes back.

### Control flow

**Tail calls.** `jmp target` instead of `call target; ret`. Saves 1 byte (the `ret`) and is used wherever a function's last action is calling another.

**Fallthrough between functions.** When functions are laid out contiguously, the last instruction of one can fall through into the next. For example, `alloc_fork` falls through into shared `alloc_stem` code, and `abstract` falls through into `s2` falls through into `make_app`.

**Shared suffixes.** Two code paths that end with the same instruction sequence can merge into a single copy. For example, multiple reduction rule cases share a `call apply; xchg; pop; jmp apply` tail.

**Fused parse+eval.** In minbin variants, `parse_eval()` is a single recursive function. Reading a `0` bit triggers `parse_eval(); parse_eval(); apply()` — parsing and reduction are inseparable. This replaces three separate components (parser, apply-loop, identity builder) with one function.


## Further reading

- [A Whirlwind Tutorial on Creating Really Teensy ELF Executables](https://www.muppetlabs.com/~breadbox/software/tiny/teensy.html) — Brian Raiter's classic
- [Justine Tunney's Actually Portable Executable](https://justine.lol/ape.html) — ELF header tricks at larger scale
- Linux kernel `fs/binfmt_elf.c` — authoritative source for which header fields matter
