#!/bin/sh
# build.sh — Build all (or specific) tree calculus evaluator variants.
#
# Usage:
#   ./build.sh              # build all variants, both strategies
#   ./build.sh x64 x64-jay  # build specific variants
#   ./build.sh --clean      # remove all built binaries
#   ./build.sh --sizes      # print size table from built binaries
#   ./build.sh --test       # run tests (delegates to node test.mjs)
#
# Each variant produces two binaries in bin/:
#   <variant>                — standard toolchain build, post-processed
#   <variant>-header-hackery — hand-crafted ELF with code in header fields
#
# Requires: gcc, strip, readelf, awk, python3, as, ld, objcopy
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$SCRIPT_DIR/src"
BIN="$SCRIPT_DIR/bin"

ALL_VARIANTS="x64 x64-ternary x64-jay x64-noid x64-minbin x64-minbin-deep x64-vm"

mkdir -p "$BIN"

# ─── Architecture ─────────────────────────────────────────────────

ARCH="x86-64"

# ─── Standard build (post-processed toolchain ELF) ───────────────────

build_standard() {
  local variant="$1"
  local src_file="$SRC/$variant.s"
  local out="$BIN/$variant"

  gcc -nostdlib -static -Wl,-n -Wl,--build-id=none "$src_file" -o "$out"
  strip --strip-all "$out"

  eval "$(readelf -lW "$out" 2>/dev/null | awk '/LOAD/{printf "P_OFFSET=%s P_FILESZ=%s", $2, $5}')"

  # Rebuild a minimal ELF around the extracted .text:
  #   - phdr relocated to offset 48, overlapping the ehdr tail. The only
  #     ehdr fields the kernel still reads there are e_phentsize (=56, at
  #     [54:56], doubled by p_flags' high half) and e_phnum (=1, at [56:58],
  #     doubled by p_offset=1's low bytes). e_flags/e_ehsize/e_sh* are
  #     kernel-ignored and absorb the rest.
  #   - p_offset=1 with p_vaddr=0x400001, so file offset F maps to
  #     0x400000+F (and stays Rosetta-compatible: offset%page == vaddr%page).
  #   - p_align [96:104] is ignored for ET_EXEC and holds code bytes.
  #   - p_memsz [88:96] holds code bytes too when their little-endian value
  #     is a valid size (>= file+heap, < the user address-space limit and
  #     thus ending in 00 00); otherwise a real constant is written and
  #     code starts at 96 instead of 88.
  python3 -c "
import struct
d = bytearray(open('$out','rb').read())
code = bytes(d[$P_OFFSET:$P_OFFSET+$P_FILESZ])
V = 0x400000
HEAP = 0x20000000            # matches .lcomm heap in the sources
msz_val = struct.unpack_from('<Q', code, 0)[0]
memsz_ok = len(code) >= 16 and msz_val >= HEAP + len(code) + 96 and msz_val < 1 << 46
code_pos = 88 if memsz_ok else 96
size = code_pos + len(code)
out = bytearray(d[0:48])                              # ehdr head (magic..e_shoff)
struct.pack_into('<Q', out, 24, V + code_pos)         # e_entry
struct.pack_into('<Q', out, 32, 48)                   # e_phoff
struct.pack_into('<Q', out, 40, 0)                    # e_shoff (ignored)
out += struct.pack('<I', 1)                           # p_type = PT_LOAD
out += struct.pack('<I', 0x00380007)                  # p_flags = RWX; high half = e_phentsize = 56
out += struct.pack('<Q', 1)                           # p_offset = 1; low bytes = e_phnum = 1
out += struct.pack('<Q', V + 1)                       # p_vaddr (== p_offset mod page)
out += struct.pack('<Q', 0)                           # p_paddr (ignored)
out += struct.pack('<Q', size - 1)                    # p_filesz
if memsz_ok:
    out += code[0:16]                                 # p_memsz + p_align = first 16 code bytes
    out += code[16:]
else:
    out += struct.pack('<Q', HEAP + size)             # p_memsz
    out += code[0:8]                                  # p_align = first 8 code bytes
    out += code[8:]
assert len(out) == size
open('$out','wb').write(out)
"
  chmod +x "$out"
  echo "  $variant: $(wc -c < "$out") bytes"
}

# ─── Header-hackery build (hand-crafted flat ELF) ────────────────────

build_header_hackery() {
  local variant="$1"
  local src_file="$SRC/$variant-header-hackery.s"
  local out="$BIN/$variant-header-hackery"

  as  "$src_file" -o "$out.o"
  ld  -Ttext=0x400000 "$out.o" -o "$out.elf"
  objcopy -O binary -j .text "$out.elf" "$out"
  chmod +x "$out"
  rm -f "$out.o" "$out.elf"

  echo "  $variant-header-hackery: $(wc -c < "$out") bytes"
}

# ─── Build one variant (both strategies) ──────────────────────────────

build_variant() {
  build_standard "$1"
  build_header_hackery "$1"
}

# ─── Sizes table ──────────────────────────────────────────────────────

print_sizes() {
  printf "%-15s %8s %16s\n" "variant" "eval" "header-hackery"
  printf "%-15s %8s %16s\n" "-------" "----" "--------------"
  for v in $ALL_VARIANTS; do
    sz1=$(wc -c < "$BIN/$v" 2>/dev/null || echo "?")
    sz2=$(wc -c < "$BIN/$v-header-hackery" 2>/dev/null || echo "?")
    printf "%-15s %6s B %14s B\n" "$v" "$sz1" "$sz2"
  done
}

# ─── Main ─────────────────────────────────────────────────────────────

case "${1:-}" in
  --clean)
    rm -f "$BIN"/*
    echo "Cleaned all binaries."
    exit 0
    ;;
  --sizes)
    print_sizes | tee "$SCRIPT_DIR/sizes.txt"
    exit 0
    ;;
  --test)
    shift
    exec node "$SCRIPT_DIR/test.mjs" "$@"
    ;;
esac

variants="${*:-$ALL_VARIANTS}"

for v in $variants; do
  build_variant "$v"
done

# Update sizes.txt after a full build
if [ -z "$*" ] || [ "$*" = "$ALL_VARIANTS" ]; then
  print_sizes > "$SCRIPT_DIR/sizes.txt"
fi
