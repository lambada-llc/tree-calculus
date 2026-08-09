#!/usr/bin/env node
// Node runner for main.wasm — see ../run.mjs

import { run } from "../run.mjs";

await run(new URL("./main.wasm", import.meta.url));
