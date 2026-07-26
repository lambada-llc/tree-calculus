import { assert_equal, Evaluator } from "../common.mjs";
import { DagModule, box, is_private, is_label } from "./module.mjs";
import { environment } from "./env.mjs";
import { link, order, interface_of, DuplicateExportError, DependencyCycleError } from "./link.mjs";
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
  test_canonicalize();
  test_interface();
  test_link();
  test_environment();
  test_file();
}
