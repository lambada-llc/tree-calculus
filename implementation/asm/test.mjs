#!/usr/bin/env node
//
// Minimal test runner for asm variants.
//
// Usage:
//   node test.mjs              # test all variants
//   node test.mjs x64 x64-jay  # test specific variants

import { execSync } from "node:child_process";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { existsSync } from "node:fs";

const asmDir = dirname(fileURLToPath(import.meta.url));

// ─── Variant table ──────────────────────────────────────────────────

const VARIANTS = {
  "x64":              { format: "ternary", rules: "triage" },
  "x64-jay":          { format: "ternary", rules: "jay"    },
  "x64-noid":         { format: "ternary", rules: "triage" },
  "x64-minbin":       { format: "minbin",  rules: "triage" },
  "x64-minbin-deep":  { format: "minbin",  rules: "triage" },
};

// ─── Binary execution ───────────────────────────────────────────────

const binDir = resolve(asmDir, "bin");

function runBin(bin, input) {
  try {
    return execSync(bin, { cwd: binDir, input, timeout: 10000 }).toString().trim();
  } catch (e) {
    if (e.status === 139 || e.status === 134 || e.status === 133) return null;
    throw e;
  }
}

function ensureBuilt(variant) {
  if (existsSync(`${binDir}/${variant}`)) return;
  console.log(`Building ${variant} ...`);
  execSync(`./build.sh ${variant}`, { cwd: asmDir, stdio: "inherit" });
}

// ─── Ternary ↔ minbin conversion ────────────────────────────────────

function ternaryToMinbin(ternary) {
  let pos = 0;
  function convert() {
    const tag = ternary[pos++];
    if (tag === '0') return '1';
    if (tag === '1') return '01' + convert();
    if (tag === '2') return '001' + convert() + convert();
    throw new Error(`unexpected '${tag}' in ternary`);
  }
  return convert();
}

function buildMinbinInput(ternaryArgs) {
  let expr = ternaryToMinbin(ternaryArgs[0]);
  for (let i = 1; i < ternaryArgs.length; i++)
    expr = '0' + expr + ternaryToMinbin(ternaryArgs[i]);
  return expr;
}

// ─── Test cases ─────────────────────────────────────────────────────
//
// Hard-coded [name, ternaryArgs, expectedTernary] triples.
// Triage identity: fork(fork(△,△),△) = "22000"  (built by x64/x64-noid)
// Jay identity:    fork(stem(△),stem(△)) = "21010"

const TRIAGE_TESTS = [
  ["identity",    ["21100", "10"],    "10"],
  ["K combinator", ["10", "0", "10"], "0" ],
];

const JAY_TESTS = [
  ["identity",     ["21010", "10"],    "10"],
  ["K selects fst", ["10", "10", "0"], "10"],
];

const MINBIN_ROUNDTRIPS = ["1", "011", "00111"];

// ─── Main ───────────────────────────────────────────────────────────

const requested = process.argv.slice(2);
const variantsToTest = requested.length > 0
  ? requested.filter(v => { if (!VARIANTS[v]) { console.error(`unknown variant: ${v}`); process.exitCode = 1; } return !!VARIANTS[v]; })
  : Object.keys(VARIANTS);

let totalPass = 0, totalFail = 0;

for (const name of variantsToTest) {
  const cfg = VARIANTS[name];
  ensureBuilt(name);
  const tests = cfg.rules === "jay" ? JAY_TESTS : TRIAGE_TESTS;

  for (const [bin, label] of [[name, "eval"], [`${name}-header-hackery`, "header-hackery"]]) {
    const binPath = `${binDir}/${bin}`;
    if (!existsSync(binPath)) {
      console.log(`\n=== ${name} / ${label}: SKIPPED (not built) ===`);
      continue;
    }
    console.log(`\n=== ${name} / ${label} ===`);
    let pass = 0, fail = 0;

    function check(testName, actual, expected) {
      if (actual === null) {
        console.log(`✗ ${testName}: SEGFAULT`);
        process.exitCode = 1; fail++;
      } else if (actual === expected) {
        console.log(`✓ ${testName}: ${actual}`);
        pass++;
      } else {
        console.log(`✗ ${testName}: expected=${expected} actual=${actual}`);
        process.exitCode = 1; fail++;
      }
    }

    for (const [testName, ternaryArgs, expectedTernary] of tests) {
      let input, expected;
      if (cfg.format === "minbin") {
        input = buildMinbinInput(ternaryArgs) + "\n";
        expected = ternaryToMinbin(expectedTernary);
      } else {
        input = ternaryArgs.join("\n") + "\n";
        expected = expectedTernary;
      }
      check(testName, runBin(binPath, input), expected);
    }

    if (cfg.format === "minbin") {
      for (const mb of MINBIN_ROUNDTRIPS)
        check(`roundtrip: ${mb}`, runBin(binPath, mb + "\n"), mb);
    }

    console.log(`  ${pass} passed, ${fail} failed`);
    totalPass += pass;
    totalFail += fail;
  }
}

console.log(`\n=== Total: ${totalPass} passed, ${totalFail} failed ===`);
