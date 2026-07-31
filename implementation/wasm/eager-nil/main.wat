(module
  ;; ============================================================
  ;; Tree Calculus Evaluator — WebAssembly (WASI)
  ;;
  ;; A reference implementation of triage calculus in pure WAT.
  ;; Reads ternary-encoded trees from stdin (one per line), left-folds
  ;; application starting from the identity tree, and writes the result
  ;; to stdout in ternary encoding.
  ;;
  ;; Same evaluator as ../eager-value, differing only in how a node is stored:
  ;; that one tags each node with its arity, this one does not.
  ;;
  ;; Ternary encoding:
  ;;   '0'           = △            (leaf)
  ;;   '1' <tree>    = △ <tree>     (stem)
  ;;   '2' <t1> <t2> = △ <t1> <t2>  (fork)
  ;;
  ;; Memory layout (1024 pages = 64 MB):
  ;;   0x00–0x0F     WASI iovec scratch + I/O byte
  ;;   0x10+         Node storage (8 bytes each, bump-allocated)
  ;;
  ;; Nodes are tagless and constant-size: two i32 child slots, with the arity
  ;; discriminated by null (0) children instead of a tag — the "nil"
  ;; representation of ../../cpp/eager-ternary-nil-32.hpp:
  ;;
  ;;   <0> <0>      — leaf
  ;;   <child> <0>  — stem
  ;;   <a> <b>      — fork  (both non-null)
  ;;
  ;; A node is named by its byte offset p into node storage, so its slots live
  ;; at 0x10+p (left) and 0x14+p (right).  Since 0 is the null sentinel, offset
  ;; 0 is reserved and the one shared leaf sits at offset 8; both are covered by
  ;; WASM's zero-initialized memory, so neither is ever written.
  ;;
  ;; Versus a tagged node (type, u, v): 8 bytes instead of 12, so a third less
  ;; node memory and one store fewer per allocation, and no tag load or
  ;; validity check on the dispatch path — every two-slot pattern decodes to
  ;; some arity.
  ;;
  ;; The memory saving is exact and engine-independent (recursive fib n=28:
  ;; 832 MB of arena against eager-value's 1216 MB).  Time is not where this
  ;; pays: against an equivalently-dispatching tagged evaluator the two run
  ;; within a few percent, this one a hair slower.  See the README — in
  ;; particular before reading anything into eager-value's wasmtime time,
  ;; which is dominated by its br_table dispatch rather than its node size.
  ;; ============================================================

  ;; ---- WASI imports ----
  (import "wasi_snapshot_preview1" "fd_read"
    (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))

  ;; ---- Memory: 64 MB ----
  (memory (export "memory") 1024)
  ;; Pre-initialize iovec at 0x00: { buf_ptr=0x0C, buf_len=1 }
  ;; Byte at 0x0C is used for single-byte I/O.  nread/nwritten at 0x08.
  (data (i32.const 0x00) "\0C\00\00\00\01\00\00\00")

  ;; ---- Globals ----
  (global $leaf i32 (i32.const 8))            ;; the one and only leaf node
  (global $free_from (mut i32) (i32.const 8)) ;; byte offset of last allocated node
                                              ;; (starts at the leaf)
  (global $eof       (mut i32) (i32.const 0)) ;; set to 1 when stdin is exhausted
  (global $mem_limit (mut i32) (i32.const 67108864)) ;; current memory ceiling (1024 pages × 65536)

  ;; ============================================================
  ;; Node storage
  ;; ============================================================

  (func $get_u (param $i i32) (result i32) (i32.load offset=0x10 (local.get $i)))
  (func $get_v (param $i i32) (result i32) (i32.load offset=0x14 (local.get $i)))

  ;; Allocate a node with given u, v children (v = 0 makes it a stem).
  ;; Grows memory by 64 MB chunks when the arena is exhausted.
  (func $alloc (param $u i32) (param $v i32) (result i32)
    (global.set $free_from (i32.add (global.get $free_from) (i32.const 8)))
    (if (i32.ge_u (i32.add (global.get $free_from) (i32.const 24))
                  (global.get $mem_limit))
      (then
        (if (i32.eq (memory.grow (i32.const 1024)) (i32.const -1))
          (then (unreachable)))
        (global.set $mem_limit
          (i32.add (global.get $mem_limit) (i32.const 67108864)))))
    (i32.store offset=0x10 (global.get $free_from) (local.get $u))
    (i32.store offset=0x14 (global.get $free_from) (local.get $v))
    (global.get $free_from))

  ;; ============================================================
  ;; Core: apply  (eager reduction)
  ;; ============================================================
  ;; Each triage reads both child slots and discriminates on their nullity:
  ;; left null → leaf, right null → stem, neither → fork.

  (func $apply (param $a i32) (param $b i32) (result i32)
    (local $u i32) (local $y i32)  ;; a = △ u y
    (local $w i32) (local $x i32)  ;; u = △ w x  (stem case: w is u's only child)
    (local $d i32) (local $e i32)  ;; b = △ d e

    (local.set $u (call $get_u (local.get $a)))
    (local.set $y (call $get_v (local.get $a)))

    ;; a is leaf  (0a): △ · b  →  △ b
    (if (i32.eqz (local.get $u))
      (then (return (call $alloc (local.get $b) (i32.const 0)))))

    ;; a is stem  (0b): (△ u) · b  →  △ u b
    (if (i32.eqz (local.get $y))
      (then (return (call $alloc (local.get $u) (local.get $b)))))

    ;; a is fork: inspect u
    (local.set $w (call $get_u (local.get $u)))
    (local.set $x (call $get_v (local.get $u)))

    ;; u is leaf  (rule 1): (△ △ y) · z  →  y
    (if (i32.eqz (local.get $w))
      (then (return (local.get $y))))

    ;; u is stem  (rule 2): (△ (△ w) y) · z  →  (w·z) · (y·z)
    (if (i32.eqz (local.get $x))
      (then (return (call $apply
        (call $apply (local.get $w) (local.get $b))
        (call $apply (local.get $y) (local.get $b))))))

    ;; u is fork  (rules 3): triage on b
    (local.set $d (call $get_u (local.get $b)))
    (local.set $e (call $get_v (local.get $b)))

    ;; b is leaf  (3a): (△ (△ w x) y) · △  →  w
    (if (i32.eqz (local.get $d))
      (then (return (local.get $w))))

    ;; b is stem  (3b): (△ (△ w x) y) · (△ d)  →  x · d
    (if (i32.eqz (local.get $e))
      (then (return (call $apply (local.get $x) (local.get $d)))))

    ;; b is fork  (3c): (△ (△ w x) y) · (△ d e)  →  (y·d) · e
    (call $apply
      (call $apply (local.get $y) (local.get $d))
      (local.get $e)))

  ;; ============================================================
  ;; Byte-at-a-time I/O  (WASI)
  ;; ============================================================
  ;; iovec at 0x00 is pre-initialized by the data segment above.

  ;; Read one byte from stdin. On EOF, sets $eof and returns '0'.
  (func $read_byte (result i32)
    (if (result i32)
        (i32.or  ;; nonzero if fd_read errored OR read 0 bytes
          (call $fd_read (i32.const 0) (i32.const 0x00)
                         (i32.const 1) (i32.const 0x08))
          (i32.eqz (i32.load (i32.const 0x08))))
      (then (global.set $eof (i32.const 1)) (i32.const 0x30))
      (else (i32.load8_u (i32.const 0x0C)))))

  ;; Write one byte to stdout.
  (func $write_byte (param $b i32)
    (i32.store8 (i32.const 0x0C) (local.get $b))
    (drop (call $fd_write (i32.const 1) (i32.const 0x00)
                          (i32.const 1) (i32.const 0x08))))

  ;; ============================================================
  ;; Parse ternary encoding  (stdin → node offset)
  ;; ============================================================
  ;; Reads bytes one at a time, skipping anything outside '0','1','2'.
  ;; On EOF, $read_byte returns '0', so this returns leaf.

  (func $parse_tree (result i32)
    (loop $skip
      (block $is_fork
      (block $is_stem
      (block $is_leaf
        (br_table $is_leaf $is_stem $is_fork $skip
          (i32.sub (call $read_byte) (i32.const 0x30)))
      )
        (return (global.get $leaf))
      )
        (return (call $alloc (call $parse_tree) (i32.const 0)))
      )
      (return (call $alloc (call $parse_tree) (call $parse_tree)))
    )
    (unreachable))

  ;; ============================================================
  ;; Emit ternary encoding  (node offset → stdout)
  ;; ============================================================

  (func $emit_tree (param $x i32)
    (local $u i32) (local $v i32)
    (local.set $u (call $get_u (local.get $x)))
    (local.set $v (call $get_v (local.get $x)))
    ;; Tag byte: '0' + arity.  A non-null right child implies a non-null left
    ;; one, so counting the non-null slots is exactly the arity.
    (call $write_byte (i32.add (i32.const 0x30)
      (i32.add (i32.ne (local.get $u) (i32.const 0))
               (i32.ne (local.get $v) (i32.const 0)))))
    (if (local.get $u) (then (call $emit_tree (local.get $u))))
    (if (local.get $v) (then (call $emit_tree (local.get $v)))))

  ;; ============================================================
  ;; Entry point (_start for WASI)
  ;; ============================================================

  (func (export "_start")
    (local $result i32)
    (local $tree i32)

    ;; Initialize result to identity tree: △ (△ (△ △)) △
    (local.set $result
      (call $alloc
        (call $alloc (call $alloc (global.get $leaf) (i32.const 0)) (i32.const 0))
        (global.get $leaf)))

    ;; Left-fold application over each input tree
    (block $end
    (loop $next
      (local.set $tree (call $parse_tree))
      (br_if $end (global.get $eof))
      (local.set $result
        (call $apply (local.get $result) (local.get $tree)))
      (br $next)
    ))

    ;; Emit result and trailing newline
    (call $emit_tree (local.get $result))
    (call $write_byte (i32.const 0x0A)))
)
