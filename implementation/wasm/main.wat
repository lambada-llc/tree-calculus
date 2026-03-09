(module
  ;; ============================================================
  ;; Tree Calculus Evaluator — WebAssembly (WASI)
  ;;
  ;; A reference implementation of triage calculus in pure WAT.
  ;; Reads ternary-encoded trees from stdin (one per line), left-folds
  ;; application, and writes the result to stdout in ternary encoding.
  ;;
  ;; Ternary encoding:
  ;;   '0'           = △            (leaf)
  ;;   '1' <tree>    = △ <tree>     (stem)
  ;;   '2' <t1> <t2> = △ <t1> <t2>  (fork)
  ;;
  ;; Memory layout (1024 pages = 64 MB):
  ;;   0x00000       16 B     WASI iovec scratch
  ;;   0x00010     ~512 KB    I/O buffer
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

  ;; ---- Globals ----
  (global $free_from (mut i32) (i32.const 1))   ;; next free node (0 = leaf)
  (global $parse_pos (mut i32) (i32.const 0))   ;; read cursor in I/O buf
  (global $emit_pos  (mut i32) (i32.const 0))   ;; write cursor in I/O buf

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
    (block $a_default
    (block $a_fork
    (block $a_stem
    (block $a_leaf
      (br_table $a_leaf $a_stem $a_fork $a_default
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
      (block $u_default
      (block $u_fork
      (block $u_stem
      (block $u_leaf
        (br_table $u_leaf $u_stem $u_fork $u_default
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
        (block $b_default
        (block $b_fork
        (block $b_stem
        (block $b_leaf
          (br_table $b_leaf $b_stem $b_fork $b_default
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
          (return (call $apply
            (call $apply
              (call $get_v (local.get $a))
              (call $get_u (local.get $b)))
            (call $get_v (local.get $b))))
        )
        (unreachable)    ;; b_default
      )
      (unreachable)      ;; u_default
    )
    (unreachable)        ;; a_default
  )

  ;; ============================================================
  ;; Parse ternary encoding  (from I/O buffer → node index)
  ;; ============================================================
  ;; Reads bytes starting at global $parse_pos and advances it.

  (func $parse_tree (result i32)
    (local $tag i32)
    (local $u i32)
    ;; Read one character and convert to tag: '0'→0, '1'→1, '2'→2
    (local.set $tag
      (i32.sub (i32.load8_u (global.get $parse_pos))
               (i32.const 0x30)))
    (global.set $parse_pos
      (i32.add (global.get $parse_pos) (i32.const 1)))

    (block $default
    (block $is_fork
    (block $is_stem
    (block $is_leaf
      (br_table $is_leaf $is_stem $is_fork $default
        (local.get $tag))
    )
      ;; '0' → leaf
      (return (i32.const 0))
    )
      ;; '1' → stem(parse_tree())
      (return (call $alloc_stem (call $parse_tree)))
    )
      ;; '2' → fork(parse_tree(), parse_tree())
      (local.set $u (call $parse_tree))
      (return (call $alloc_fork (local.get $u) (call $parse_tree)))
    )
    (unreachable))

  ;; ============================================================
  ;; Emit ternary encoding  (node index → I/O buffer)
  ;; ============================================================

  ;; Write one byte; flush to stdout if buffer is getting full.
  (func $emit_byte (param $byte i32)
    (i32.store8 (global.get $emit_pos) (local.get $byte))
    (global.set $emit_pos
      (i32.add (global.get $emit_pos) (i32.const 1)))
    ;; Flush if approaching node storage region
    (if (i32.ge_u (global.get $emit_pos) (i32.const 0x70000))
      (then
        (call $write_stdout
          (i32.const 0x10)
          (i32.sub (global.get $emit_pos) (i32.const 0x10)))
        (global.set $emit_pos (i32.const 0x10)))))

  ;; Recursively emit a tree as ternary encoding.
  (func $emit_tree (param $x i32)
    (block $default
    (block $is_fork
    (block $is_stem
    (block $is_leaf
      (br_table $is_leaf $is_stem $is_fork $default
        (call $get_type (local.get $x)))
    )
      ;; leaf → '0'
      (call $emit_byte (i32.const 0x30))
      (return)
    )
      ;; stem → '1' then child
      (call $emit_byte (i32.const 0x31))
      (call $emit_tree (call $get_u (local.get $x)))
      (return)
    )
      ;; fork → '2' then left then right
      (call $emit_byte (i32.const 0x32))
      (call $emit_tree (call $get_u (local.get $x)))
      (call $emit_tree (call $get_v (local.get $x)))
      (return)
    )
    (unreachable))

  ;; ============================================================
  ;; I/O helpers  (WASI)
  ;; ============================================================

  ;; Read all of stdin into I/O buffer at offset 0x10.
  ;; Returns total number of bytes read.
  (func $read_all_stdin (result i32)
    (local $total i32)
    (local $nread i32)
    (local.set $total (i32.const 0))
    (block $done
    (loop $again
      ;; Set up iovec: buf = 0x10 + total, len = capacity remaining
      (i32.store (i32.const 0x00)
        (i32.add (i32.const 0x10) (local.get $total)))
      (i32.store (i32.const 0x04)
        (i32.sub (i32.const 0x7FFF0) (local.get $total)))
      ;; fd_read(stdin=0, iovs=0x00, iovs_len=1, nread_ptr=0x08)
      (br_if $done
        (call $fd_read
          (i32.const 0) (i32.const 0x00) (i32.const 1) (i32.const 0x08)))
      ;; Check for EOF (nread == 0)
      (local.set $nread (i32.load (i32.const 0x08)))
      (br_if $done (i32.eqz (local.get $nread)))
      (local.set $total
        (i32.add (local.get $total) (local.get $nread)))
      (br $again)
    ))
    (local.get $total))

  ;; Write $len bytes starting at $buf to stdout.
  (func $write_stdout (param $buf i32) (param $len i32)
    (i32.store (i32.const 0x00) (local.get $buf))
    (i32.store (i32.const 0x04) (local.get $len))
    ;; fd_write(stdout=1, iovs=0x00, iovs_len=1, nwritten_ptr=0x08)
    (drop (call $fd_write
      (i32.const 1) (i32.const 0x00) (i32.const 1) (i32.const 0x08))))

  ;; ============================================================
  ;; Entry point (_start for WASI)
  ;; ============================================================

  (func (export "_start")
    (local $input_end i32)   ;; past-the-end address of input data
    (local $pos i32)         ;; current scan position
    (local $ch i32)          ;; current character
    (local $has_result i32)  ;; 1 once we have parsed at least one tree
    (local $result i32)      ;; accumulated application result
    (local $tree i32)        ;; most recently parsed tree

    ;; Step 1: Read all of stdin
    (local.set $input_end
      (i32.add (i32.const 0x10) (call $read_all_stdin)))

    ;; Step 2: Process line by line
    (local.set $pos (i32.const 0x10))
    (local.set $has_result (i32.const 0))

    (block $end
    (loop $next_line
      ;; Skip newlines / carriage returns between trees
      (block $found
      (loop $skip
        (br_if $end
          (i32.ge_u (local.get $pos) (local.get $input_end)))
        (local.set $ch (i32.load8_u (local.get $pos)))
        (br_if $found
          (i32.and
            (i32.ne (local.get $ch) (i32.const 0x0A))   ;; '\n'
            (i32.ne (local.get $ch) (i32.const 0x0D)))) ;; '\r'
        (local.set $pos
          (i32.add (local.get $pos) (i32.const 1)))
        (br $skip)
      ))

      ;; If past end, we're done
      (br_if $end
        (i32.ge_u (local.get $pos) (local.get $input_end)))

      ;; Parse one tree from current position
      (global.set $parse_pos (local.get $pos))
      (local.set $tree (call $parse_tree))
      (local.set $pos (global.get $parse_pos))

      ;; Left-fold: result = apply(result, tree)
      (if (local.get $has_result)
        (then
          (local.set $result
            (call $apply (local.get $result) (local.get $tree))))
        (else
          (local.set $result (local.get $tree))
          (local.set $has_result (i32.const 1))))

      (br $next_line)
    ))

    ;; Step 3: Emit result to stdout
    (if (local.get $has_result)
      (then
        ;; Reuse I/O buffer (input already consumed)
        (global.set $emit_pos (i32.const 0x10))
        (call $emit_tree (local.get $result))
        ;; Append newline
        (call $emit_byte (i32.const 0x0A))
        ;; Flush any remaining buffered output
        (if (i32.gt_u (global.get $emit_pos) (i32.const 0x10))
          (then
            (call $write_stdout
              (i32.const 0x10)
              (i32.sub (global.get $emit_pos) (i32.const 0x10))))))))
)
