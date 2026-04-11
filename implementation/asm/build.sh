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
# Requires: gcc, strip, dd, truncate, readelf, awk, python3, as, ld, objcopy
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$SCRIPT_DIR/src"
BIN="$SCRIPT_DIR/bin"

ALL_VARIANTS="x64 x64-jay x64-noid x64-minbin x64-minbin-deep"

mkdir -p "$BIN"

# ─── Architecture ─────────────────────────────────────────────────

ARCH="x86-64"

# ─── Standard build (post-processed toolchain ELF) ───────────────────

build_standard() {
  local variant="$1"
  local src_file="$SRC/$variant.s"
  local out="$BIN/$variant"

  E_SHOFF_SEEK=40  E_SHOFF_LEN=8
  E_SHNUM_SEEK=60  E_SHSTRNDX_SEEK=62
  EHDR_SZ=64  PHDR_SZ=56  OVERLAP=8
  STRUCT_FMT=Q  ENTRY_OFF=24  PHOFF_OFF=32
  P_FILESZ_OFF=32  P_MEMSZ_OFF=40

  gcc -nostdlib -static -Wl,-n -Wl,--build-id=none "$src_file" -o "$out"
  strip --strip-all "$out"

  eval "$(readelf -lW "$out" 2>/dev/null | awk '/LOAD/{printf "P_OFFSET=%s P_FILESZ=%s", $2, $5}')"
  TEXT_END=$(( P_OFFSET + P_FILESZ ))

  dd if=/dev/zero of="$out" bs=1 seek=$E_SHOFF_SEEK count=$E_SHOFF_LEN conv=notrunc 2>/dev/null
  dd if=/dev/zero of="$out" bs=1 seek=$E_SHNUM_SEEK count=2 conv=notrunc 2>/dev/null
  dd if=/dev/zero of="$out" bs=1 seek=$E_SHSTRNDX_SEEK count=2 conv=notrunc 2>/dev/null
  truncate -s "$TEXT_END" "$out"

  python3 -c "
import struct
d = bytearray(open('$out','rb').read())
FMT = '<$STRUCT_FMT'
e_entry = struct.unpack_from(FMT, d, $ENTRY_OFF)[0]
phdr = d[$EHDR_SZ:$EHDR_SZ+$PHDR_SZ]
code = d[$EHDR_SZ+$PHDR_SZ:]
old_fsz = struct.unpack_from(FMT, phdr, $P_FILESZ_OFF)[0]
old_msz = struct.unpack_from(FMT, phdr, $P_MEMSZ_OFF)[0]
out = bytearray(d[0:$EHDR_SZ - $OVERLAP])
struct.pack_into(FMT, out, $ENTRY_OFF, e_entry - $OVERLAP)
struct.pack_into(FMT, out, $PHOFF_OFF, $EHDR_SZ - $OVERLAP)
struct.pack_into(FMT, phdr, $P_FILESZ_OFF, old_fsz - $OVERLAP)
struct.pack_into(FMT, phdr, $P_MEMSZ_OFF, old_msz - $OVERLAP)
out.extend(phdr)
out.extend(code)
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
