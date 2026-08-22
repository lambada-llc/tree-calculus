import { mkdtempSync, rmSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";
import { assert_equal, Evaluator } from "../common.mjs";
import { DagModule, box, is_private, is_label } from "./module.mjs";
import { environment } from "./env.mjs";
import { link, order, interface_of, DuplicateExportError, DependencyCycleError } from "./link.mjs";
import { fingerprint } from "./fingerprint.mjs";
import { store } from "./cache.mjs";
import { to_file, of_file } from "../format/file.mjs";
import formatter_ternary from "../format/ternary.mjs";

// Evaluator to use for this test -- any valid one works
import e from "../evaluator/eager-stacks.mjs";
type TTree = typeof e extends Evaluator<infer T> ? T : never;

function assert_throws(f: () => unknown, expected: string, test_case: string) {
  let message: string | null = null;
  try { f(); } catch (error: any) { message = String(error?.message ?? error); }
  console.assert(
    message !== null && message.includes(expected),
    `expected an error containing "${expected}", got ${message === null ? 'no error' : `"${message}"`}, test: ${test_case}`);
}

// --- Parsing and printing ---

function test_parse() {
  const text = 'k △ △\ni △ (\nfalse k\nfalse\n';
  assert_equal(4, DagModule.parse(text).lines.length, 'blank lines dropped');
  assert_equal(
    'k △ △\nfalse k\nfalse\n',
    DagModule.parse('k △ △\nx :i k\nfalse x\nfalse\n').toString(),
    'internal aliases are absorbed');
  assert_equal(
    'k △ △\nx :i k\nfalse x\nfalse\n',
    DagModule.parse('k △ △\nx :i k\nfalse x\nfalse\n', { absorb_internal_aliases: false }).toString(),
    'internal aliases are kept when asked');

  // References bind to the definition above them, so rebinding a name leaves
  // earlier uses alone.
  const shadowed = DagModule.parse('a △ △\nb a\na △\nc a\n');
  assert_equal(shadowed.lines[1][1], shadowed.lines[0][0], 'b refers to the first a');
  assert_equal(shadowed.lines[3][1], shadowed.lines[2][0], 'c refers to the second a');
}

// --- Naming conventions ---

function test_conventions() {
  assert_equal(true, is_private('_helper'), '_helper is private');
  assert_equal(true, is_private('Bool._helper'), 'privacy is judged on the local part');
  assert_equal(false, is_private('_'), 'a bare _ is not private');
  assert_equal(false, is_private('helper'), 'helper is not private');
  assert_equal(true, is_label(':i'), ':i is a label');
  assert_equal(false, is_label('i'), 'i is not a label');
}

// --- Qualifying ---

function test_qualify() {
  const qualified = DagModule
    .parse('not △ △\n_helper △\nnot △\n:t not\n', { absorb_internal_aliases: false })
    .qualify('Bool.')
    .toString();
  assert_equal(
    'Bool.not:0 △ △\nBool._helper:1 △\nBool.not △\n:t Bool.not\n',
    qualified,
    'last public definition exports, everything else stays private');

  assert_equal(
    'Bool.x △\n__ENV△ △\n',
    DagModule.parse('x △\n__ENV△ △\n').qualify('Bool.', {
      reserved: name => is_label(name) || name.startsWith('__ENV'),
    }).toString(),
    'reserved names are left alone');
}

// --- Extracting ---

function test_extract() {
  const library = 'k △ △\nunused △ k\ni △ (△ (△ △)) △\n';
  assert_equal(
    'i △ (△ (△ △)) △\ni\n',
    DagModule.parse(library).extract('i').toString(['i']),
    'only what the symbol is built from is kept, closed by naming it');
  assert_equal(
    'k △ △\nunused △ k\n',
    DagModule.parse(library).extract('unused').toString(),
    'a definition brings along what it refers to, by name');

  // Extracting a shadowed name yields its last definition, like a reference would.
  assert_equal(
    'a △\n',
    DagModule.parse('a △ △\nb a\na △\n').extract('a').toString(),
    'the latest definition wins');

  assert_throws(() => DagModule.parse('a △\n').extract('nope'), 'unknown symbol: nope',
    'extracting something undefined is an error');

  // Several roots at once. What they share is kept once — `k` is below both,
  // and appears in the result the one time it was written.
  const shared = 'k △ △\nleft △ k\nright k △\ndead △ (△ △)\n';
  assert_equal(
    'k △ △\nleft △ k\nright k △\n',
    DagModule.parse(shared).extract('left', 'right').toString(),
    'two roots keep what both are built from, once');
  assert_equal(
    DagModule.parse(shared).extract('left', 'right').toString(),
    DagModule.parse(shared).extract('right', 'left').toString(),
    'the order roots are named in does not show');

  // Naming what stays is how one says "everything but": nothing else is built
  // from `dead`, so nothing keeps it.
  assert_equal(
    'k △ △\nleft △ k\nright k △\n',
    DagModule.parse(shared).extract('k', 'left', 'right').toString(),
    'dropping a root drops what only it reached');

  assert_equal(
    '\n',
    DagModule.parse(shared).extract().toString(),
    'no roots keeps nothing');
}

// --- Canonicalizing ---

function assert_same_value(expected: string, actual: string, symbol: string, test_case: string) {
  assert_equal(
    formatter_ternary.to(e, environment(e, expected)(symbol)),
    formatter_ternary.to(e, environment(e, actual)(symbol)),
    test_case);
}

function test_canonicalize() {
  // The same fork built twice becomes one node.
  const module = DagModule.parse('a △ △\nb △ △\nresult a b\n:r result\n');
  const canonical = module.canonicalize().toString();
  assert_equal(
    '1 △ △\n2 1 1\n:r 2\n',
    canonical,
    'identical forks are shared');
  assert_same_value('a △ △\nb △ △\nresult a b\n:r result\n', canonical, ':r',
    'canonicalizing preserves the value');

  // Two names for one value do not defeat sharing: `x` and `y` build the same
  // node, one of them by way of a name bound to the leaf.
  assert_equal(
    '__ENV△ △\n1 △ △\nr 1\n:r r\n',
    DagModule.parse('__ENV△ △\nx △ __ENV△\ny △ △\nr x\n:r r\n').canonicalize().toString(),
    'a name bound to the leaf shares with △ itself');
}

// --- Linking ---

const fragment = (name: string, text: string) => ({ name, text });

function test_interface() {
  const { exports, imports } = interface_of('k △ △\nnot k\nhelper:0 Other.thing\n');
  assert_equal('not', exports.join(','), 'two-word definitions of ordinary names export');
  assert_equal('△,Other.thing', imports.join(','), 'anything not defined above is imported');
}

function test_link() {
  const bool = fragment('bool', 'Bool.true △ △\n');
  const uses_bool = fragment('uses', 'x Bool.true\nUses.x x\n');
  assert_equal(
    'bool,uses',
    order([uses_bool, bool]).map(f => f.name).join(','),
    'dependencies are linked first');
  assert_equal(
    'Bool.true △ △\nx Bool.true\nUses.x x\n',
    link([uses_bool, bool]),
    'linking concatenates in dependency order');

  assert_throws(
    () => order([fragment('a', 'dup △\n'), fragment('b', 'dup △\n')]),
    'duplicate exports',
    'two modules cannot export the same name');
  assert_equal(
    true,
    (() => { try { order([fragment('a', 'dup △\n'), fragment('b', 'dup △\n')]); } catch (x) { return x instanceof DuplicateExportError; } return false; })(),
    'duplicate exports raise DuplicateExportError');

  assert_throws(
    () => order([fragment('a', 'A.x B.y\n'), fragment('b', 'B.y A.x\n')]),
    'dependency cycle',
    'mutually dependent modules cannot be ordered');
  assert_equal(
    true,
    (() => { try { order([fragment('a', 'A.x B.y\n'), fragment('b', 'B.y A.x\n')]); } catch (x) { return x instanceof DependencyCycleError; } return false; })(),
    'cycles raise DependencyCycleError');

  // Order must not depend on the order the modules were handed over.
  const many = [fragment('c', 'C.z B.y\n'), fragment('a', 'A.x △ △\n'), fragment('b', 'B.y A.x\n')];
  assert_equal(
    order(many).map(f => f.name).join(','),
    order([...many].reverse()).map(f => f.name).join(','),
    'linking is deterministic');
}

function test_partition() {
  // `one` and `two` each build on the shared `k`, and `two` alone uses `2`.
  const library = 'k △ △\n1 k △\none 1\n2 k k\ntwo 2\nkept △ k\n';
  const { shared, exclusive } = DagModule.parse(library).partition(['one', 'two']);
  assert_equal(
    'k △ △\nkept △ k\n',
    shared.toString(),
    'what a non-root reaches stays shared');
  assert_equal('1 k △\none 1\n', exclusive.get('one')!.toString(),
    'a root takes the definitions only it reaches, referring to shared ones by name');
  assert_equal('2 k k\ntwo 2\n', exclusive.get('two')!.toString(), 'and so does the other');

  // The point of the split: shared + one root reproduces that root's extract.
  const module = DagModule.parse(library);
  assert_equal(
    module.extract('two').toString(['two']),
    DagModule.parse(module.partition(['two']).shared.toString()
      + module.partition(['two']).exclusive.get('two')!.toString(['two']))
      .extract('two').toString(['two']),
    'shared plus a root is what extracting that root gives');

  assert_equal(
    'shared △ △\nboth shared shared\none both\ntwo both\n',
    (({ shared, exclusive }) => shared.toString() + exclusive.get('one')!.toString()
      + exclusive.get('two')!.toString())(
      DagModule.parse('shared △ △\nboth shared shared\none both\ntwo both\n')
        .partition(['one', 'two'])),
    'what two roots both reach is shared rather than duplicated');

  assert_equal(
    'a △ △\nterminator a\na\n',
    (({ shared }) => shared.toString())(
      DagModule.parse('a △ △\nterminator a\na\n').partition([])),
    'a module with no roots is entirely shared, terminator included');

  assert_throws(() => DagModule.parse('a △\n').partition(['nope']), 'unknown symbol: nope',
    'partitioning on something undefined is an error');

  // `a` means the first definition where `one` uses it and the second below —
  // a distinction position keeps and a split would lose, so the earlier one is
  // renamed and `one` follows it.
  const shadowing = DagModule.parse('a △ △\n1 a △\none 1\na △\nkept a\n');
  const split = shadowing.partition(['one']);
  assert_equal(
    'a:s0 △ △\na △\nkept a\n',
    split.shared.toString(),
    'a shadowed definition is given a name of its own');
  assert_equal('1 a:s0 △\none 1\n', split.exclusive.get('one')!.toString(),
    'and what referred to it says so');
  assert_same_value(
    'a △ △\n1 a △\none 1\na △\nkept a\n',
    split.shared.toString() + split.exclusive.get('one')!.toString(),
    'one',
    'renaming preserves the value');
}

// --- Environments ---

function test_environment() {
  const get = environment(e, 'k △ △\nstem_k △ k\nsk △ stem_k\ni sk △\n');
  assert_equal('21100', formatter_ternary.to(e, get('i')), 'symbols evaluate to their trees');
  assert_equal('10', formatter_ternary.to(e, get('k')), 'every binding is available, not just the last');
  assert_throws(() => environment(e, 'x missing\n'), 'unbound symbol: missing',
    'referencing something undefined is an error');
  assert_throws(() => environment(e, 'x △\n', { origin: 'some.dag' }, )('nope'),
    'unbound symbol: nope', 'looking up something undefined is an error');
  assert_throws(() => environment(e, 'x missing\n', { origin: 'some.dag' }), 'some.dag:1:',
    'errors while reading point at the line');

  assert_equal('21100', formatter_ternary.to(e, get.reduce('x sk △\nx\n')),
    'an expression reduces against the module');
  assert_throws(() => get('x'), 'unbound symbol: x',
    'and leaves nothing of its own behind');
  assert_throws(() => get.reduce('x k △\n'), 'not terminated by a value',
    'an expression that names no value is an error');
}

// --- Fingerprints ---

function test_fingerprint() {
  const hex = (b?: Buffer) => b?.toString('hex') ?? null;

  // A fingerprint addresses the term, not the DAG around it: however a term is
  // spelled — different ids, extra aliases, more or less sharing — it keeps
  // its address. That is the property the reduction cache stands on.
  assert_equal(
    hex(fingerprint('a △ △\nx a △\nx\n').value),
    hex(fingerprint('1 △ △\n2 1 △\nalso 2\nalso\n').value),
    'the same term fingerprints the same, whatever the lines look like');
  assert_equal(
    false,
    hex(fingerprint('x △ △\nx\n').value) === hex(fingerprint('s △ △\ny △ s\ny\n').value),
    'different terms fingerprint differently');
  assert_equal(
    hex(fingerprint('k △ △\nk\n').fingerprints.get('k')),
    hex(fingerprint('k △ △\nalias k\nalias\n').value),
    'an alias is the term it names');

  // References mean the definition above them, so a rebound name tells the
  // uses before and after apart.
  const shadowed = fingerprint('a △ △\nbefore a a\na △ a\nafter a a\n').fingerprints;
  assert_equal(
    false,
    hex(shadowed.get('before')) === hex(shadowed.get('after')),
    'a use before a rebinding is not a use after it');

  // An expression read against a module resolves what it does not define
  // through `outer`, exactly as reduction scopes it.
  const module = fingerprint('lib △ △\n').fingerprints;
  assert_equal(
    hex(fingerprint('own lib △\nown\n', name => module.get(name)).value),
    hex(fingerprint('lib △ △\nown lib △\nown\n').value),
    'outer resolution sees the module');
  assert_throws(() => fingerprint('x missing △\nx\n'), 'unbound symbol: missing',
    'a name nobody defines is an error, not a guess');
}

// --- The cache store ---

function test_cache_store() {
  const had = process.env.TREE_CALCULUS_CACHE;
  const directory = mkdtempSync(join(tmpdir(), 'tc-cache-test-'));
  try {
    delete process.env.TREE_CALCULUS_CACHE;
    assert_equal(null, store('reduce-test') as unknown, 'no cache directory, no store');

    process.env.TREE_CALCULUS_CACHE = directory;
    const st = store('reduce-test')!;
    const key = Buffer.from('00ff', 'hex');
    assert_equal(false, st.has(key), 'a fresh store is empty');
    assert_equal(null, st.get(key), 'and get says so too');
    st.put(key, 'payload');
    assert_equal(true, st.has(key), 'a put entry is found');
    assert_equal('payload', st.get(key)!.toString('utf8'), 'and comes back whole');
    assert_equal(true, st.path(key).endsWith('00ff'), 'entries are named by their key');
  } finally {
    if (had === undefined) delete process.env.TREE_CALCULUS_CACHE;
    else process.env.TREE_CALCULUS_CACHE = had;
    rmSync(directory, { recursive: true, force: true });
  }
}

// --- Files ---

function test_file() {
  const file = { name: 'hello.txt', media_type: 'text/plain', bytes: new Uint8Array([104, 105]) };
  const round_tripped = to_file(e, of_file(e, file));
  assert_equal('hello.txt', round_tripped?.name ?? null, 'file name round-trips');
  assert_equal('text/plain', round_tripped?.media_type ?? null, 'media type round-trips');
  assert_equal('hi', new TextDecoder().decode(round_tripped?.bytes), 'bytes round-trip');

  assert_equal(null, to_file(e, e.leaf), 'a leaf is not a file');
  assert_equal(
    null,
    to_file(e, of_file(e, { ...file, name: 'no-extension' })),
    'a name that does not look like a filename is not a file');
}

export function test() {
  test_parse();
  test_conventions();
  test_qualify();
  test_extract();
  test_partition();
  test_canonicalize();
  test_interface();
  test_link();
  test_environment();
  test_fingerprint();
  test_cache_store();
  test_file();
}
