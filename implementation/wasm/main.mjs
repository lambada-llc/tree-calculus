#!/usr/bin/env node
// Node.js WASI runner for main.wasm

import { WASI } from "node:wasi";
import { readFile } from "node:fs/promises";

const wasi = new WASI({ version: "preview1" });
const { instance } = await WebAssembly.instantiate(
  await readFile(new URL("./main.wasm", import.meta.url)),
  wasi.getImportObject()
);
wasi.start(instance);
