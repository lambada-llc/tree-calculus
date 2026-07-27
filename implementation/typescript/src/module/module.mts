// DAG modules — "open" DAGs that define named symbols.
//
// The DAG format (see ../../../../conventions/) writes a tree as a sequence of
// let-bindings terminated by a value:
//
//   k △ △
//   i △ (△ (△ △)) △
//   i
//
// A *module* is the same format used one step more loosely: it need not
// terminate in a value, and it may reference symbols it does not define. That
// makes a DAG a namespace, and makes it possible to compile a program in pieces
// and link the pieces later.
//
// Naming conventions (shared by every operation here and by `link`):
//
//   △          the leaf, always spelled this way
//   :name      a *label* — never exported, never namespaced
//   name:n     an *internal* name; any name containing ':' stays module-private
//   _name      *private* — defined for local use, not part of the interface
//   name       *exported* — visible to other modules once linked
//
// Boxes
// -----
// Parsing does not produce strings, it produces *boxes*. A box is a binding
// site: the head of a definition line gets a fresh box, and every reference
// resolves to the box of the most recent definition of that name. Rebinding a
// name therefore leaves earlier references pointing at the earlier box, which is
// what makes shadowing work — and what lets `qualify` rename a definition and
// all of its references by writing to a single object.

export const LEAF = '△';

/** A binding site. References share the box of the definition they resolve to. */
export type Box = { symbol: string };
/** One line of a DAG: `[head, left, right]`, `[head, target]` or `[value]`. */
export type Line = Box[];

export function box(symbol: string): Box {
  return { symbol };
}

/** Names that can be referred to symbolically, as opposed to `△` and numeric ids. */
export function is_symbol_name(s: string): boolean {
  return /^[:a-zA-Z]/.test(s);
}

/** A label is a marker rather than part of a module's interface. */
export function is_label(name: string): boolean {
  return name.startsWith(':');
}

/**
 * Private names are defined for local use only. The local part is what counts,
 * so `Bool._helper` is private while a bare `_` (a conventional "don't care"
 * name) is not.
 */
export function is_private(name: string): boolean {
  const local = name.slice(name.lastIndexOf('.') + 1);
  return local !== '_' && local.startsWith('_');
}

export interface ParseOptions {
  /**
   * Treat `name :i target` as a pure renaming: bind `name` to `target`'s box
   * without emitting a line. Compilers emit these to give a value a second
   * name; absorbing them keeps the module's line count down. On by default.
   */
  absorb_internal_aliases?: boolean;
}

export interface QualifyOptions {
  /** Names left untouched. Labels always are; a compiler's own reserved names can be added. */
  reserved?: (name: string) => boolean;
}

export class DagModule {
  /**
   * The module's lines, in order. Mutable on purpose: callers that need to
   * rewrite a module (naming its entry points, say) work on this directly, and
   * box identity carries the binding structure for them.
   */
  lines: Line[] = [];

  /**
   * Render back to DAG text. `entries` are appended as bare lines, which is how
   * a module is closed into a plain DAG naming one value.
   */
  toString(entries: string[] = []): string {
    const rendered = this.lines.map(line => line.map(b => b.symbol).join(' '));
    rendered.push(...entries);
    return rendered.join('\n') + '\n';
  }

  static parse(text: string, options: ParseOptions = {}): DagModule {
    const { absorb_internal_aliases = true } = options;
    const module = new DagModule();
    const latest: { [symbol: string]: Box } = Object.create(null);

    const resolve = (symbol: string) => latest[symbol] ?? (latest[symbol] = box(symbol));

    for (const raw of text.split('\n')) {
      const trimmed = raw.trim();
      if (!trimmed) continue;
      const words = trimmed.split(/\s+/);

      if (absorb_internal_aliases && words.length === 3 && words[1] === ':i') {
        latest[words[0]] = resolve(words[2]);
        continue;
      }

      const is_definition = words.length === 2 || words.length === 3;
      const head = is_definition ? box(words[0]) : null;
      module.lines.push(words.map((word, i) => (i === 0 && head) ? head : resolve(word)));
      if (head) latest[head.symbol] = head;
    }

    return module;
  }

  /** The box a reference to `name` resolves to: its last definition, if any. */
  definition(name: string): Box | null {
    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      if (line.length > 1 && line[0].symbol === name) return line[0];
    }
    return null;
  }

  /**
   * Keep only the definitions `symbol` is built from, dropping everything else
   * the module happens to carry. Linking a library produces one big module; this
   * takes a single value back out of it.
   *
   * One backwards pass suffices: a reference resolves to a definition above it,
   * so by the time a line is reached, every line that could need it has been
   * seen. Names are kept as they are — a reference by name says which symbol was
   * used, which an id no longer does.
   */
  extract(symbol: string): DagModule {
    const root = this.definition(symbol);
    if (!root) throw new Error(`unknown symbol: ${symbol}`);

    const needed = new Set<Box>([root]);
    const kept: Line[] = [];
    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      if (line.length === 1 || !needed.has(line[0])) continue;
      for (let j = 1; j < line.length; j++) needed.add(line[j]);
      kept.push(line);
    }

    const out = new DagModule();
    out.lines = kept.reverse();
    return out;
  }

  /**
   * Namespace this module's exports under `prefix`, so that `not` compiled from
   * `bool/bool.lamb` can become `Bool.not` without colliding with any other
   * module's `not`.
   *
   * A name is exported when it is public and this is its last definition —
   * anything shadowed later was only scaffolding for what came after. Everything
   * else is prefixed *and* given a `:n` suffix, which keeps it unique across
   * modules while marking it as not part of the interface.
   *
   * Only heads are rewritten; references follow because they share the box.
   */
  qualify(prefix: string, options: QualifyOptions = {}): this {
    const { reserved = is_label } = options;

    const last_definition = new Map<string, number>();
    this.lines.forEach((line, i) => {
      if (line.length === 2 && !reserved(line[0].symbol))
        last_definition.set(line[0].symbol, i);
    });

    let internal = 0;
    this.lines.forEach((line, i) => {
      const head = line[0].symbol;
      const qualifiable =
        (line.length === 2 || (line.length === 3 && /^[a-zA-Z_]/.test(head)))
        && !reserved(head);
      if (!qualifiable) return;
      const exported = !is_private(head) && last_definition.get(head) === i;
      line[0].symbol = exported ? `${prefix}${head}` : `${prefix}${head}:${internal++}`;
    });

    return this;
  }

  /**
   * Hash-cons into a new module whose nodes have globally unique numeric ids.
   *
   * Modules compiled separately each number their nodes from scratch, so
   * concatenating them yields a DAG with the same id meaning different things in
   * different places, and with the same subtree built many times over. This
   * resolves both: every distinct node is defined exactly once, under an id no
   * other node shares.
   *
   * Sharing is keyed on resolved ids rather than on how a reference happens to
   * be spelled, so two names for one value collapse to one node.
   */
  canonicalize(): DagModule {
    const LEAF_ID = 0;
    const ids = new Map<Box, number>();
    for (const line of this.lines)
      for (const b of line)
        if (b.symbol === LEAF) ids.set(b, LEAF_ID);

    let next_id = 1;
    const forks = new Map<string, number>();
    const named = new Set<string>();
    const aliased = new Map<string, number | undefined>();
    const out = new DagModule();

    // How a reference is written: the leaf always as △, then any name already
    // defined above, then the node's id.
    const ref = (b: Box): string => {
      const id = ids.get(b);
      if (id === LEAF_ID) return LEAF;
      if (is_symbol_name(b.symbol) && named.has(b.symbol)) return b.symbol;
      return id === undefined ? b.symbol : String(id);
    };
    // What sharing is decided on. Unresolved references — symbols this module
    // does not define — can only be told apart by name.
    const key = (b: Box): string => {
      const id = ids.get(b);
      return id === undefined ? `?${b.symbol}` : String(id);
    };

    for (const line of this.lines) {
      if (line.length === 3) {
        const [head, left, right] = line;
        const fork_key = `${key(left)} ${key(right)}`;
        const shared = forks.get(fork_key);
        if (shared !== undefined) {
          ids.set(head, shared);
        } else {
          const id = next_id++;
          forks.set(fork_key, id);
          const line_out = [box(String(id)), box(ref(left)), box(ref(right))];
          ids.set(head, id);
          out.lines.push(line_out);
        }
        // Labels name a node the caller wants to find again; keep them.
        if (is_label(head.symbol)) {
          const id = ids.get(head)!;
          out.lines.push([box(head.symbol), box(id === LEAF_ID ? LEAF : String(id))]);
        }

      } else if (line.length === 2) {
        const [head, target] = line;
        const target_id = ids.get(target);
        if (target_id !== undefined) ids.set(head, target_id);
        const id = ids.get(head);
        const name = head.symbol;
        const previous = aliased.get(name);
        if (previous !== undefined && previous === id) continue; // already said
        if (previous === undefined) {
          if (is_symbol_name(name)) named.add(name);
        } else {
          // Redefined to something else — the name is no longer a stable
          // reference, so later lines must use ids.
          named.delete(name);
        }
        aliased.set(name, id);
        out.lines.push([box(name), box(ref(target))]);

      } else if (line.length === 1) {
        out.lines.push([box(ref(line[0]))]);
      }
    }

    return out;
  }
}
