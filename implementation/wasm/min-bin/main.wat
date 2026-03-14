(module
  (import "wasi_snapshot_preview1" "fd_read" (func $r (param i32 i32 i32 i32)(result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $o (param i32 i32 i32 i32)(result i32)))
  (memory (export "memory") 256)
  (global $h (mut i32)(i32.const 0))
  (global $c (mut i32)(i32.const 0))

  ;; Copy balanced tree from $p to write head $h. Uses ASCII 0x30='0', 0x31='1'.
  ;; Depth tracks via (byte & 1): '1'->depth-1, '0'->depth+1.
  (func $t (param $p i32)(result i32)(local $d i32)
    (local.set $d(i32.const 1))
    (loop $l(if(local.get $d)(then
      (i32.store8(global.get $h)(i32.load8_u(local.get $p)))
      (local.set $d(i32.add(local.get $d)(select(i32.const -1)(i32.const 1)(i32.and(i32.load8_u(local.get $p))(i32.const 1)))))
      (local.set $p(i32.add(local.get $p)(i32.const 1)))
      (global.set $h(i32.add(global.get $h)(i32.const 1)))(br $l))))
    (local.get $p))

  ;; Skip: measure tree without writing
  (func $s (param $p i32)(result i32)
    (global.get $h)(local.set $p(call $t(local.get $p)))(global.set $h)(local.get $p))

  ;; Emit '0' (0x30)
  (func $z (i32.store8(global.get $h)(i32.const 0x30))(global.set $h(i32.add(global.get $h)(i32.const 1))))

  ;; Tree walker: pattern match and rewrite. All bytes are ASCII 0x30/0x31.
  ;; Check low bit (&1) to distinguish: 0x31&1=1 (leaf/'1'), 0x30&1=0 (app/'0').
  (func $w (param $p i32)(result i32)
    (local $a i32)(local $b i32)(local $c2 i32)
    ;; Leaf: p[0]&1 != 0
    (if(i32.and(i32.load8_u(local.get $p))(i32.const 1))(then
      (i32.store8(global.get $h)(i32.const 0x31))(global.set $h(i32.add(global.get $h)(i32.const 1)))
      (return(i32.add(local.get $p)(i32.const 1)))))
    ;; Redex: p[0..3] == 0x31303030 (little-endian: '0','0','0','1')
    (if(i32.eq(i32.load align=1(local.get $p))(i32.const 0x31303030))
      (then
        (local.set $a(call $s(i32.add(local.get $p)(i32.const 4))))
        (local.set $b(call $s(local.get $a)))
        (local.set $c2(call $s(local.get $b)))
        (block $red
        ;; V=leaf: p[4]&1
        (if(i32.and(i32.load8_u offset=4(local.get $p))(i32.const 1))(then
          (drop(call $t(local.get $a)))(br $red)))
        ;; V=stem: p[5]&1
        (if(i32.and(i32.load8_u offset=5(local.get $p))(i32.const 1))(then
          (call $z)(call $z)
          (drop(call $t(i32.add(local.get $p)(i32.const 6))))(drop(call $t(local.get $b)))
          (call $z)(drop(call $t(local.get $a)))(drop(call $t(local.get $b)))
          (br $red)))
        ;; W=leaf: b[0]&1
        (if(i32.and(i32.load8_u(local.get $b))(i32.const 1))(then
          (drop(call $t(i32.add(local.get $p)(i32.const 7))))(br $red)))
        ;; W=stem: b[1]&1
        (if(i32.and(i32.load8_u offset=1(local.get $b))(i32.const 1))(then
          (call $z)
          (drop(call $t(call $s(i32.add(local.get $p)(i32.const 7)))))
          (drop(call $t(i32.add(local.get $b)(i32.const 2))))
          (br $red)))
        ;; W=fork: b[2]&1
        (if(i32.and(i32.load8_u offset=2(local.get $b))(i32.const 1))(then
          (call $z)(call $z)
          (drop(call $t(local.get $a)))(drop(call $t(i32.add(local.get $b)(i32.const 3))))
          (drop(call $t(call $s(i32.add(local.get $b)(i32.const 3)))))
          (br $red)))
        ;; Non-value W: copy fork verbatim, recurse on W
        (call $z)(drop(call $t(i32.add(local.get $p)(i32.const 1))))
        (return(call $w(local.get $b))))
        ;; Shared epilogue for all 5 reduction rules
        (global.set $c(i32.const 1))(return(local.get $c2))))
    ;; Non-redex application
    (call $z)
    (local.set $p(call $w(i32.add(local.get $p)(i32.const 1))))
    (call $w(local.get $p)))

  ;; Identity tree in ASCII: "001010111" = 0x30 0x30 0x31 0x30 0x31 0x30 0x31 0x31 0x31
  (data (i32.const 16) "\30\30\31\30\31\30\31\31\31")

  ;; _start: read stdin (already ASCII), scan lines, apply+reduce, write output
  (func (export "_start")(local $u i32)(local $n i32)(local $l i32)(local $d i32)(local $p i32)(local $e i32)
    (local.set $u(i32.const 16))(local.set $n(i32.const 9))
    ;; Bulk read all stdin into 0x7F0000
    (i32.store(i32.const 0)(i32.const 0x7F0000))
    (i32.store(i32.const 4)(i32.const 0x100000))
    (drop(call $r(i32.const 0)(i32.const 0)(i32.const 1)(i32.const 8)))
    (local.set $e(i32.add(i32.const 0x7F0000)(i32.load(i32.const 8))))
    ;; No conversion needed — input is already ASCII!
    ;; Scan lines: bytes are '0'(0x30), '1'(0x31), '\n'(0x0A)
    ;; Line bytes have value >= 0x30; delimiter '\n' has value 0x0A < 0x30
    (local.set $p(i32.const 0x7F0000))
    (loop $lp(if(i32.lt_u(local.get $p)(local.get $e))(then
      ;; Find line end: scan from $p while byte >= 0x30
      (local.set $d(local.get $p))
      (block $el(loop $ll
        (br_if $el(i32.ge_u(local.get $d)(local.get $e)))
        (br_if $el(i32.lt_u(i32.load8_u(local.get $d))(i32.const 0x30)))
        (local.set $d(i32.add(local.get $d)(i32.const 1)))(br $ll)))
      (local.set $l(i32.sub(local.get $d)(local.get $p)))
      (if(local.get $l)(then
        ;; Build application: '0' + current_tree + input_line
        (local.set $d(i32.sub(i32.const 0x400010)(local.get $u)))
        (i32.store8(local.get $d)(i32.const 0x30))
        (memory.copy(i32.add(local.get $d)(i32.const 1))(local.get $u)(local.get $n))
        (memory.copy(i32.add(i32.add(local.get $d)(i32.const 1))(local.get $n))(local.get $p)(local.get $l))
        (local.set $u(local.get $d))
        ;; Reduce to fixpoint: ping-pong bufA↔bufB
        (loop $rl
          (local.set $d(i32.sub(i32.const 0x400010)(local.get $u)))
          (global.set $c(i32.const 0))(global.set $h(local.get $d))
          (drop(call $w(local.get $u)))
          (local.set $n(i32.sub(global.get $h)(local.get $d)))
          (local.set $u(local.get $d))
          (br_if $rl(global.get $c)))))
      (local.set $p(i32.add(local.get $d)(i32.const 1)))
      (br $lp))))
    ;; Output: already ASCII, just append newline and write
    (i32.store8(i32.add(local.get $u)(local.get $n))(i32.const 10))
    (i32.store(i32.const 0)(local.get $u))
    (i32.store(i32.const 4)(i32.add(local.get $n)(i32.const 1)))
    (drop(call $o(i32.const 1)(i32.const 0)(i32.const 1)(i32.const 8))))
)
