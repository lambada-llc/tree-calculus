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
  ;;   0x00000       16 B     WASI iovec scratch + I/O byte
  ;;   0x80000     ~63 MB     Node storage
  ;;
  ;; Each node i is 12 bytes at 0x80000 + i*12:
  ;;   +0  type (i32):  0=leaf, 1=stem, 2=fork
  ;;   +4  u    (i32):  left child index
  ;;   +8  v    (i32):  right child index
  ;; Node 0 is the unique leaf (zero-initialized by WASM).
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
  (global $free_from (mut i32) (i32.const 1))   ;; next free node (0 = leaf)
  (global $eof       (mut i32) (i32.const 0))   ;; set to 1 when stdin is exhausted

  ;; ============================================================
  ;; Node storage
  ;; ============================================================

  ;; Address of node i in linear memory.
  (func $node_addr (param $i i32) (result i32)
    (i32.add (i32.const 0x80000)
             (i32.mul (local.get $i) (i32.const 12))))

  (func $get_type (param $i i32) (result i32)
    (i32.load (call $node_addr (local.get $i))))

  (func $get_u (param $i i32) (result i32)
    (i32.load offset=4 (call $node_addr (local.get $i))))

  (func $get_v (param $i i32) (result i32)
    (i32.load offset=8 (call $node_addr (local.get $i))))

  ;; Allocate a stem node: △ u
  (func $alloc_stem (param $u i32) (result i32)
    (local $idx i32)
    (local $addr i32)
    (local.set $idx (global.get $free_from))
    (global.set $free_from (i32.add (local.get $idx) (i32.const 1)))
    (local.set $addr (call $node_addr (local.get $idx)))
    (i32.store          (local.get $addr) (i32.const 1))
    (i32.store offset=4 (local.get $addr) (local.get $u))
    (local.get $idx))

  ;; Allocate a fork node: △ u v
  (func $alloc_fork (param $u i32) (param $v i32) (result i32)
    (local $idx i32)
    (local $addr i32)
    (local.set $idx (global.get $free_from))
    (global.set $free_from (i32.add (local.get $idx) (i32.const 1)))
    (local.set $addr (call $node_addr (local.get $idx)))
    (i32.store          (local.get $addr) (i32.const 2))
    (i32.store offset=4 (local.get $addr) (local.get $u))
    (i32.store offset=8 (local.get $addr) (local.get $v))
    (local.get $idx))

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
      (return (call $alloc_stem (local.get $b)))
    )
      ;; a is stem  (0b): (△ u) · b  →  fork(u, b)
      (return (call $alloc_fork
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

  ;; Read one byte from stdin.  Returns the byte, or -1 on EOF/error.
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
  ;; Reads bytes one at a time from stdin, skipping non-'0','1','2' bytes.
  ;; On EOF, $read_byte sets $eof and returns '0', so this returns leaf.

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
        (return (call $alloc_stem (call $parse_tree)))
      )
      (return (call $alloc_fork (call $parse_tree) (call $parse_tree)))
    )
    (unreachable))

  ;; ============================================================
  ;; Emit ternary encoding  (node index → stdout)
  ;; ============================================================

  (func $emit_tree (param $x i32)
    (block $default
    (block $is_fork
    (block $is_stem
    (block $is_leaf
      (br_table $is_leaf $is_stem $is_fork $default
        (call $get_type (local.get $x)))
    )
      ;; leaf → '0'
      (call $write_byte (i32.const 0x30))
      (return)
    )
      ;; stem → '1' then child
      (call $write_byte (i32.const 0x31))
      (call $emit_tree (call $get_u (local.get $x)))
      (return)
    )
      ;; fork → '2' then left then right
      (call $write_byte (i32.const 0x32))
      (call $emit_tree (call $get_u (local.get $x)))
      (call $emit_tree (call $get_v (local.get $x)))
      (return)
    )
    (unreachable))

  ;; ============================================================
  ;; Entry point (_start for WASI)
  ;; ============================================================

  (func (export "_start")
    (local $result i32)
    (local $tree i32)

    ;; Initialize result to identity tree: △ (△ (△ △)) △
    (local.set $result
      (call $alloc_fork
        (call $alloc_stem (call $alloc_stem (i32.const 0)))
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
