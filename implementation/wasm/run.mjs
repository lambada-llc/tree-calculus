// Shared Node runner for the WASM variants.
//
// The modules only import fd_read and fd_write, so this provides just those
// two as plain JS instead of going through node:wasi — whose experimental
// uvwasi binding segfaults nondeterministically (~40% of runs on Node 22,
// macOS and Linux alike, even on trivial input; the benchmark's "exit 139"
// rows). As a bonus, plain imports need no Node 21+ and V8 turns any trap
// or stack overflow into a catchable RuntimeError instead of a crash.
//
// The modules themselves remain fully WASI-compatible: `wasmtime main.wasm`
// runs the same binaries unchanged.

import { readFileSync, readSync, writeSync } from "node:fs";

export async function run(wasmUrl) {
  let memory;

  // Synchronous retry-sleep for EAGAIN on non-blocking pipes.
  const wait = () => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 1);

  const imports = {
    wasi_snapshot_preview1: {
      // Fill the single iovec from stdin; 0 bytes signals EOF.
      fd_read(fd, iovs, iovsLen, nreadPtr) {
        const dv = new DataView(memory.buffer);
        const ptr = dv.getUint32(iovs, true);
        const len = dv.getUint32(iovs + 4, true);
        let n = 0;
        for (;;) {
          try {
            n = readSync(fd, new Uint8Array(memory.buffer, ptr, len));
            break;
          } catch (e) {
            if (e.code === "EAGAIN") { wait(); continue; }
            if (e.code === "EOF") break;
            return 8; // WASI errno "badf" — the module treats errors as EOF
          }
        }
        dv.setUint32(nreadPtr, n, true);
        return 0;
      },
      // Drain the single iovec to stdout/stderr, tolerating partial writes.
      fd_write(fd, iovs, iovsLen, nwrittenPtr) {
        const dv = new DataView(memory.buffer);
        const ptr = dv.getUint32(iovs, true);
        const len = dv.getUint32(iovs + 4, true);
        let done = 0;
        while (done < len) {
          try {
            done += writeSync(fd, new Uint8Array(memory.buffer, ptr + done, len - done));
          } catch (e) {
            if (e.code === "EAGAIN") { wait(); continue; }
            return 8;
          }
        }
        dv.setUint32(nwrittenPtr, len, true);
        return 0;
      },
    },
  };

  const { instance } = await WebAssembly.instantiate(readFileSync(wasmUrl), imports);
  memory = instance.exports.memory;
  instance.exports._start();
}
