(module
  ;; ============================================================
  ;; Tree Calculus Evaluator — WebAssembly (WASI)
  ;;
  ;; A reference implementation of triage calculus in pure WAT.
  ;; Reads ternary-encoded trees from stdin (one per line), left-folds
  ;; application starting from the identity tree, and writes the result
  ;; to stdout in ternary encoding.
  ;;
  ;; Same nodes and reduction rules as ../eager-nil, but nothing recurses on
  ;; the machine stack: apply is an explicit VM loop over continuation frames
  ;; in linear memory (the WAT port of ../../cpp/eager-ternary-nil-vm-32.hpp),
  ;; and parse/emit are iterative over the same frame region.  Recursion depth
  ;; is therefore bounded by the frame region, not the host stack — which is
  ;; what lets this variant run workloads that kill the recursive variants
  ;; (see README).
  ;;
  ;; Ternary encoding:
  ;;   '0'           = △            (leaf)
  ;;   '1' <tree>    = △ <tree>     (stem)
  ;;   '2' <t1> <t2> = △ <t1> <t2>  (fork)
  ;;
  ;; Memory layout (initially 2048 pages = 128 MB):
  ;;   0x00–0x07     read iovec { 0x10000, 64 KB }  (data segment below)
  ;;   0x08          nread cell for fd_read
  ;;   0x10–0x17     write iovec (fields written at flush time)
  ;;   0x18          nwritten cell for fd_write
  ;;   0x20          root cell for the iterative parser
  ;;   0x10000       read buffer, 64 KB
  ;;   0x20000       write buffer, 64 KB
  ;;   0x30000       frame stack, 8-byte frames, grows up to 0x4000000
  ;;   0x4000000+    node storage (8 bytes each, bump-allocated)
  ;;
  ;; Nodes are the tagless "nil" representation of ../eager-nil: two i32 child
  ;; slots, arity discriminated by null (0) children:
  ;;
  ;;   <0> <0>      — leaf
  ;;   <child> <0>  — stem
  ;;   <a> <b>      — fork  (both non-null)
  ;;
  ;; A node is named by its absolute byte address, so 0 doubles as the null
  ;; sentinel and the one shared leaf sits at 0x4000000 — both zero slots,
  ;; covered by WASM's zero-initialized memory, never written.
  ;;
  ;; Frames are 8 bytes: word0 is a node with a tag in bit 0 (nodes are
  ;; 8-aligned, so the bit is free), word1 a second node where the tag needs
  ;; one.  The ~64 MB region holds 8M frames, replacing the ~10k calls the
  ;; recursive variants get from Node before the process dies.  Exhausting it
  ;; traps cleanly (unreachable) instead of segfaulting.
  ;; ============================================================

  ;; ---- WASI imports ----
  (import "wasi_snapshot_preview1" "fd_read"
    (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32)))

  ;; ---- Memory: 128 MB (64 MB frame stack + 64 MB initial arena) ----
  (memory (export "memory") 2048)
  ;; Pre-initialize read iovec at 0x00: { buf_ptr=0x10000, buf_len=0x10000 }
  (data (i32.const 0x00) "\00\00\01\00\00\00\01\00")

  ;; ---- Globals ----
  (global $leaf i32 (i32.const 0x4000000))    ;; the one and only leaf node
  (global $free_from (mut i32) (i32.const 0x4000000)) ;; address of last allocated node
                                              ;; (starts at the leaf)
  (global $eof       (mut i32) (i32.const 0)) ;; set to 1 when stdin is exhausted
  (global $mem_limit (mut i32) (i32.const 0x8000000)) ;; current memory ceiling
  (global $sp        (mut i32) (i32.const 0x30000))   ;; frame stack pointer
  (global $rd_pos    (mut i32) (i32.const 0)) ;; next unconsumed read-buffer byte
  (global $rd_len    (mut i32) (i32.const 0)) ;; read-buffer fill level
  (global $wr_pos    (mut i32) (i32.const 0)) ;; write-buffer fill level

  ;; ============================================================
  ;; Node storage
  ;; ============================================================

  (func $get_u (param $i i32) (result i32) (i32.load (local.get $i)))
  (func $get_v (param $i i32) (result i32) (i32.load offset=4 (local.get $i)))

  ;; Allocate a node with given u, v children (v = 0 makes it a stem).
  ;; Grows memory by 64 MB chunks when the arena is exhausted.
  (func $alloc (param $u i32) (param $v i32) (result i32)
    (global.set $free_from (i32.add (global.get $free_from) (i32.const 8)))
    (if (i32.ge_u (i32.add (global.get $free_from) (i32.const 16))
                  (global.get $mem_limit))
      (then
        (if (i32.eq (memory.grow (i32.const 1024)) (i32.const -1))
          (then (unreachable)))
        (global.set $mem_limit
          (i32.add (global.get $mem_limit) (i32.const 67108864)))))
    (i32.store (global.get $free_from) (local.get $u))
    (i32.store offset=4 (global.get $free_from) (local.get $v))
    (global.get $free_from))

  ;; ============================================================
  ;; Frame stack
  ;; ============================================================

  ;; Push an 8-byte frame.  Single-word users pass 0 for $w1.
  (func $push (param $w0 i32) (param $w1 i32)
    (if (i32.ge_u (global.get $sp) (i32.const 0x4000000))
      (then (unreachable)))  ;; frame region exhausted
    (i32.store (global.get $sp) (local.get $w0))
    (i32.store offset=4 (global.get $sp) (local.get $w1))
    (global.set $sp (i32.add (global.get $sp) (i32.const 8))))

  ;; ============================================================
  ;; Core: apply  (eager reduction, explicit VM loop)
  ;; ============================================================
  ;; The recursive rules become two frame types, exactly as in the C++ VM:
  ;;
  ;;   APPLY_TO(arg)              (tag bit 0): when the current computation
  ;;     produces r, begin computing apply(r, arg).
  ;;   COMPUTE_AND_APPLY(fn, arg) (tag bit 1): when the current computation
  ;;     produces r, push APPLY_TO(r) and begin computing apply(fn, arg) —
  ;;     the shape apply(apply(fn, arg), r) of the fork-stem rule.
  ;;
  ;; Each triage reads both child slots and discriminates on their nullity:
  ;; left null → leaf, right null → stem, neither → fork.

  (func $apply (param $a i32) (param $b i32) (result i32)
    (local $u i32) (local $y i32)  ;; a = △ u y
    (local $w i32) (local $x i32)  ;; u = △ w x  (stem case: w is u's only child)
    (local $d i32) (local $e i32)  ;; b = △ d e
    (local $result i32)
    (local $w0 i32)
    (local $sp i32)  ;; frame pointer, kept local on the hot path
                     ;; ($apply has the region to itself while it runs)

    (local.set $sp (i32.const 0x30000))
    (loop $reduce
      (block $dispatch  ;; ---- evaluate apply(a, b) ----
        (local.set $u (call $get_u (local.get $a)))
        (local.set $y (call $get_v (local.get $a)))

        ;; a is leaf  (0a): △ · b  →  △ b
        (if (i32.eqz (local.get $u))
          (then
            (local.set $result (call $alloc (local.get $b) (i32.const 0)))
            (br $dispatch)))

        ;; a is stem  (0b): (△ u) · b  →  △ u b
        (if (i32.eqz (local.get $y))
          (then
            (local.set $result (call $alloc (local.get $u) (local.get $b)))
            (br $dispatch)))

        ;; a is fork: inspect u
        (local.set $w (call $get_u (local.get $u)))
        (local.set $x (call $get_v (local.get $u)))

        ;; u is leaf  (rule 1): (△ △ y) · z  →  y
        (if (i32.eqz (local.get $w))
          (then
            (local.set $result (local.get $y))
            (br $dispatch)))

        ;; u is stem  (rule 2): (△ (△ w) y) · z  →  (w·z) · (y·z)
        ;; Evaluate y·z now; the frame remembers to compute w·z and combine.
        (if (i32.eqz (local.get $x))
          (then
            (if (i32.ge_u (local.get $sp) (i32.const 0x4000000))
              (then (unreachable)))  ;; frame region exhausted
            (i32.store (local.get $sp) (i32.or (local.get $w) (i32.const 1)))
            (i32.store offset=4 (local.get $sp) (local.get $b))
            (local.set $sp (i32.add (local.get $sp) (i32.const 8)))
            (local.set $a (local.get $y))
            (br $reduce)))

        ;; u is fork  (rules 3): triage on b
        (local.set $d (call $get_u (local.get $b)))
        (local.set $e (call $get_v (local.get $b)))

        ;; b is leaf  (3a): (△ (△ w x) y) · △  →  w
        (if (i32.eqz (local.get $d))
          (then
            (local.set $result (local.get $w))
            (br $dispatch)))

        ;; b is stem  (3b): (△ (△ w x) y) · (△ d)  →  x · d
        (if (i32.eqz (local.get $e))
          (then
            (local.set $a (local.get $x))
            (local.set $b (local.get $d))
            (br $reduce)))

        ;; b is fork  (3c): (△ (△ w x) y) · (△ d e)  →  (y·d) · e
        (if (i32.ge_u (local.get $sp) (i32.const 0x4000000))
          (then (unreachable)))  ;; frame region exhausted
        (i32.store (local.get $sp) (local.get $e))
        (local.set $sp (i32.add (local.get $sp) (i32.const 8)))
        (local.set $a (local.get $y))
        (local.set $b (local.get $d))
        (br $reduce)
      )

      ;; ---- dispatch result through the frame stack ----
      (if (i32.eq (local.get $sp) (i32.const 0x30000))
        (then (return (local.get $result))))
      (local.set $sp (i32.sub (local.get $sp) (i32.const 8)))
      (local.set $w0 (i32.load (local.get $sp)))
      (if (i32.and (local.get $w0) (i32.const 1))
        (then  ;; COMPUTE_AND_APPLY(fn, arg): stash result, run apply(fn, arg)
          (local.set $a (i32.and (local.get $w0) (i32.const -2)))
          (local.set $b (i32.load offset=4 (local.get $sp)))
          (i32.store (local.get $sp) (local.get $result))
          (local.set $sp (i32.add (local.get $sp) (i32.const 8))))
        (else  ;; APPLY_TO(arg): run apply(result, arg)
          (local.set $a (local.get $result))
          (local.set $b (local.get $w0))))
      (br $reduce)
    )
    (unreachable))

  ;; ============================================================
  ;; Buffered I/O  (WASI)
  ;; ============================================================
  ;; The recursive variants pay one WASI call per byte; here reads fill a
  ;; 64 KB buffer and writes drain one, so the call count is per-buffer.

  ;; Read one byte from stdin. On EOF, sets $eof and returns '0'.
  (func $read_byte (result i32)
    (local $p i32)
    (if (i32.ge_u (global.get $rd_pos) (global.get $rd_len))
      (then
        (if (i32.or  ;; nonzero if fd_read errored OR read 0 bytes
              (call $fd_read (i32.const 0) (i32.const 0x00)
                             (i32.const 1) (i32.const 0x08))
              (i32.eqz (i32.load (i32.const 0x08))))
          (then
            (global.set $eof (i32.const 1))
            (return (i32.const 0x30))))
        (global.set $rd_len (i32.load (i32.const 0x08)))
        (global.set $rd_pos (i32.const 0))))
    (local.set $p (global.get $rd_pos))
    (global.set $rd_pos (i32.add (local.get $p) (i32.const 1)))
    (i32.load8_u offset=0x10000 (local.get $p)))

  ;; Buffer one byte for stdout, flushing when the buffer fills.
  (func $write_byte (param $b i32)
    (i32.store8 offset=0x20000 (global.get $wr_pos) (local.get $b))
    (global.set $wr_pos (i32.add (global.get $wr_pos) (i32.const 1)))
    (if (i32.eq (global.get $wr_pos) (i32.const 0x10000))
      (then (call $flush))))

  ;; Drain the write buffer to stdout, tolerating short writes.
  (func $flush
    (local $ptr i32) (local $rem i32) (local $n i32)
    (local.set $ptr (i32.const 0x20000))
    (local.set $rem (global.get $wr_pos))
    (block $out
      (loop $more
        (br_if $out (i32.eqz (local.get $rem)))
        (i32.store (i32.const 0x10) (local.get $ptr))
        (i32.store (i32.const 0x14) (local.get $rem))
        (br_if $out (call $fd_write (i32.const 1) (i32.const 0x10)
                                    (i32.const 1) (i32.const 0x18)))
        (local.set $n (i32.load (i32.const 0x18)))
        (br_if $out (i32.eqz (local.get $n)))
        (local.set $ptr (i32.add (local.get $ptr) (local.get $n)))
        (local.set $rem (i32.sub (local.get $rem) (local.get $n)))
        (br $more)))
    (global.set $wr_pos (i32.const 0)))

  ;; ============================================================
  ;; Parse ternary encoding  (stdin → node address, iterative)
  ;; ============================================================
  ;; The frame stack holds addresses of child slots still awaiting a subtree;
  ;; $slot is the one being filled now (initially the root cell at 0x20).
  ;; A '1' or '2' allocates its node immediately — placing it in $slot does
  ;; not need its children, which are parsed into the pushed slots after.
  ;; Bytes outside '0','1','2' are skipped.  On EOF, $read_byte returns '0',
  ;; so pending slots drain as leaves and a top-level call returns leaf.

  (func $parse_tree (result i32)
    (local $c i32) (local $node i32) (local $slot i32)
    (local.set $slot (i32.const 0x20))
    (loop $next
      (local.set $c (i32.sub (call $read_byte) (i32.const 0x30)))
      (br_if $next (i32.gt_u (local.get $c) (i32.const 2)))

      (if (i32.eqz (local.get $c))
        (then (local.set $node (global.get $leaf)))
        (else
          (local.set $node (call $alloc (i32.const 0) (i32.const 0)))
          ;; push the child slots to fill, right first so the left parses first
          (if (i32.eq (local.get $c) (i32.const 2))
            (then (call $push (i32.add (local.get $node) (i32.const 4)) (i32.const 0))))
          (call $push (local.get $node) (i32.const 0))))
      (i32.store (local.get $slot) (local.get $node))

      ;; done when no slot awaits a subtree
      (if (i32.eq (global.get $sp) (i32.const 0x30000))
        (then (return (i32.load (i32.const 0x20)))))
      (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
      (local.set $slot (i32.load (global.get $sp)))
      (br $next))
    (unreachable))

  ;; ============================================================
  ;; Emit ternary encoding  (node address → stdout, iterative)
  ;; ============================================================
  ;; Ternary is exactly a left-first preorder walk, so the frame stack holds
  ;; nodes still to visit, right child pushed before left.

  (func $emit_tree (param $x i32)
    (local $u i32) (local $v i32)
    (call $push (local.get $x) (i32.const 0))
    (loop $next
      (global.set $sp (i32.sub (global.get $sp) (i32.const 8)))
      (local.set $x (i32.load (global.get $sp)))
      (local.set $u (call $get_u (local.get $x)))
      (local.set $v (call $get_v (local.get $x)))
      ;; Tag byte: '0' + arity.  A non-null right child implies a non-null left
      ;; one, so counting the non-null slots is exactly the arity.
      (call $write_byte (i32.add (i32.const 0x30)
        (i32.add (i32.ne (local.get $u) (i32.const 0))
                 (i32.ne (local.get $v) (i32.const 0)))))
      (if (local.get $v) (then (call $push (local.get $v) (i32.const 0))))
      (if (local.get $u) (then (call $push (local.get $u) (i32.const 0))))
      (br_if $next (i32.ne (global.get $sp) (i32.const 0x30000)))))

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
    (call $write_byte (i32.const 0x0A))
    (call $flush))
)
