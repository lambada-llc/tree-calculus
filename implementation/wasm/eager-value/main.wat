(module
  ;; ============================================================
  ;; Tree Calculus Evaluator — WebAssembly (WASI)
  ;;
  ;; A reference implementation of triage calculus in pure WAT.
  ;; Reads ternary-encoded trees from stdin (one per line), left-folds
  ;; application starting from the identity tree, and writes the result
  ;; to stdout in ternary encoding.
  ;;
  ;; Ternary encoding:
  ;;   '0'           = △            (leaf)
  ;;   '1' <tree>    = △ <tree>     (stem)
  ;;   '2' <t1> <t2> = △ <t1> <t2>  (fork)
  ;;
  ;; Memory layout (1024 pages = 64 MB):
  ;;   0x00–0x0F     WASI iovec scratch + I/O byte
  ;;   0x10+         Node storage (12 bytes each, bump-allocated)
  ;;
  ;; Each node is 12 bytes at some byte offset p:
  ;;   p+0x10  type (i32):  0=leaf, 1=stem, 2=fork
  ;;   p+0x14  u    (i32):  left child
  ;;   p+0x18  v    (i32):  right child
  ;; Node 0 (offset 0) is the unique leaf (zero-initialized by WASM).
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
  (global $free_from (mut i32) (i32.const 0)) ;; byte offset of last allocated node
                                              ;; (initialized to behind iovec)
  (global $eof       (mut i32) (i32.const 0)) ;; set to 1 when stdin is exhausted
  (global $mem_limit (mut i32) (i32.const 67108864)) ;; current memory ceiling (1024 pages × 65536)

  ;; ============================================================
  ;; Node storage
  ;; ============================================================

  (func $get_type (param $i i32) (result i32) (i32.load offset=0x10 (local.get $i)))
  (func $get_u (param $i i32) (result i32) (i32.load offset=0x14 (local.get $i)))
  (func $get_v (param $i i32) (result i32) (i32.load offset=0x18 (local.get $i)))

  ;; Allocate a node with given type, u, v fields.
  ;; Grows memory by 64 MB chunks when the arena is exhausted.
  (func $alloc (param $type i32) (param $u i32) (param $v i32) (result i32)
    (global.set $free_from (i32.add (global.get $free_from) (i32.const 12)))
    (if (i32.ge_u (i32.add (global.get $free_from) (i32.const 28))
                  (global.get $mem_limit))
      (then
        (if (i32.eq (memory.grow (i32.const 1024)) (i32.const -1))
          (then (unreachable)))
        (global.set $mem_limit
          (i32.add (global.get $mem_limit) (i32.const 67108864)))))
    (i32.store offset=0x10 (global.get $free_from) (local.get $type))
    (i32.store offset=0x14 (global.get $free_from) (local.get $u))
    (i32.store offset=0x18 (global.get $free_from) (local.get $v))
    (global.get $free_from))

  ;; ============================================================
  ;; Core: apply  (eager reduction)
  ;; ============================================================

  (func $apply (param $a i32) (param $b i32) (result i32)
    (local $u i32)

    ;; ---- dispatch on type(a) ----
    (block $a_fork
    (block $a_stem
    (block $a_leaf
      (br_table $a_leaf $a_stem $a_fork
        (call $get_type (local.get $a)))
    )
      ;; a is leaf  (0a): △ · b  →  stem(b)
      (return (call $alloc (i32.const 1) (local.get $b) (i32.const 0)))
    )
      ;; a is stem  (0b): (△ u) · b  →  fork(u, b)
      (return (call $alloc (i32.const 2)
        (call $get_u (local.get $a))
        (local.get $b)))
    )
    ;; a is fork: inspect u = a.left
    (local.set $u (call $get_u (local.get $a)))

    ;; ---- dispatch on type(u) ----
    (block $u_fork
    (block $u_stem
    (block $u_leaf
      (br_table $u_leaf $u_stem $u_fork
        (call $get_type (local.get $u)))
    )
      ;; u is leaf  (rule 1): (△ △ y) · z  →  y
      (return (call $get_v (local.get $a)))
    )
      ;; u is stem  (rule 2): (△ (△ x) y) · z  →  (x·z) · (y·z)
      (return (call $apply
        (call $apply (call $get_u (local.get $u)) (local.get $b))
        (call $apply (call $get_v (local.get $a)) (local.get $b))))
    )
    ;; u is fork  (rules 3): triage on b

    ;; ---- dispatch on type(b) ----
    (block $b_fork
    (block $b_stem
    (block $b_leaf
      (br_table $b_leaf $b_stem $b_fork
        (call $get_type (local.get $b)))
    )
      ;; b is leaf  (3a): (△ (△ w x) y) · △  →  w
      (return (call $get_u (local.get $u)))
    )
      ;; b is stem  (3b): (△ (△ w x) y) · (△ u') →  x · u'
      (return (call $apply
        (call $get_v (local.get $u))
        (call $get_u (local.get $b))))
    )
    ;; b is fork  (3c): (△ (△ w x) y) · (△ u' v') →  (y·u') · v'
    (call $apply
      (call $apply
        (call $get_v (local.get $a))
        (call $get_u (local.get $b)))
      (call $get_v (local.get $b)))
  )

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
  ;; Parse ternary encoding  (stdin → node index)
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
        (return (i32.const 0))
      )
        (return (call $alloc (i32.const 1) (call $parse_tree) (i32.const 0)))
      )
      (return (call $alloc (i32.const 2) (call $parse_tree) (call $parse_tree)))
    )
    (unreachable))

  ;; ============================================================
  ;; Emit ternary encoding  (node index → stdout)
  ;; ============================================================

  (func $emit_tree (param $x i32)
    ;; Write tag byte: type(x) + '0'
    (call $write_byte (i32.add (call $get_type (local.get $x)) (i32.const 0x30)))
    ;; If stem or fork, recurse on left child
    (if (call $get_type (local.get $x))
      (then
        (call $emit_tree (call $get_u (local.get $x)))
        ;; If fork, also recurse on right child
        (if (i32.sub (call $get_type (local.get $x)) (i32.const 1))
          (then (call $emit_tree (call $get_v (local.get $x))))))))

  ;; ============================================================
  ;; Entry point (_start for WASI)
  ;; ============================================================

  (func (export "_start")
    (local $result i32)
    (local $tree i32)

    ;; Initialize result to identity tree: △ (△ (△ △)) △
    (local.set $result
      (call $alloc (i32.const 2)
        (call $alloc (i32.const 1) (call $alloc (i32.const 1) (i32.const 0) (i32.const 0)) (i32.const 0))
        (i32.const 0)))

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
