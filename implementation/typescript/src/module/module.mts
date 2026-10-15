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
  return /^[:_a-zA-Z]/.test(s);
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
   * Keep only the definitions `symbols` are built from, as a DAG that stands on
   * its own: canonical, each symbol named, and ended by its value when there is
   * just one. Linking a library produces one big module; this takes a value
   * back out of it — or a set of them, sharing what they share rather than
   * repeating it, which is why it is not several extracts.
   *
   * Naming what stays is also how one drops something: what only the rest
   * reached is gone. No `drop` needed.
   *
   * Nodes are numbered in the order a walk from the symbols, taken in sorted
   * order, finishes their definitions — each after what it refers to — rather
   * than in the order the module has them. So what comes out depends on what
   * the symbols are built from and how that is written, not on where in the
   * module it sat: nothing else the module defines, wherever it is and whatever
   * uses it, changes a byte. Names two-word lines bind are kept as they are — a
   * reference by name says which symbol was used, which an id no longer does —
   * and `as_written` shares nodes as `canonicalize` does with it.
   *
   * That order is not the one the module's text was written for: linked
   * modules each number their nodes from scratch, and a name can be bound
   * twice. So the definitions are numbered while they still hold their boxes,
   * never read back from text, and each symbol is named again at the end —
   * the walk may have finished an earlier binding of its name after its own,
   * and a three-word line names nothing once it is numbered.
   */
  extract(symbols: string[], { as_written = false }: { as_written?: boolean } = {}): DagModule {
    const definitions = new Map<Box, Line>();
    const latest = new Map<string, Box>();
    for (const line of this.lines) {
      if (line.length !== 2 && line.length !== 3) continue;
      definitions.set(line[0], line);
      latest.set(line[0].symbol, line[0]);
    }
    const names = [...new Set(symbols)].sort();
    const roots = names.map(name => {
      if (!is_symbol_name(name)) throw new Error(`${name} is an id, which names nothing once renumbered`);
      const root = latest.get(name);
      if (!root) throw new Error(`unknown symbol: ${name}`);
      return root;
    });

    const kept = new DagModule();
    const reached = new Set<Box>();
    for (const root of roots) {
      if (reached.has(root)) continue;
      reached.add(root);
      // Depth first, by hand: a definition can sit thousands of references deep.
      const stack = [{ line: definitions.get(root)!, next: 1 }];
      while (stack.length) {
        const top = stack[stack.length - 1];
        if (top.next === top.line.length) {
          kept.lines.push(top.line);
          stack.pop();
          continue;
        }
        const child = top.line[top.next++];
        const line = definitions.get(child);
        if (!line || reached.has(child)) continue;
        reached.add(child);
        stack.push({ line, next: 1 });
      }
    }

    roots.forEach((root, i) => kept.lines.push([box(names[i]), root]));
    // A DAG ends in the name of its value; a set of them has none, so it ends
    // in nothing and is read as a library.
    if (roots.length === 1) kept.lines.push([roots[0]]);
    return kept.canonicalize({ as_written });
  }

  /**
   * Give every shadowed definition a name of its own.
   *
   * A reference means the definition above it, so a module can bind one name
   * twice and every reference still says which of the two it meant. That holds
   * only while the lines stay in the order they were written, and so is lost by
   * anything that regroups them. Renaming all but the last definition of each
   * name says the same thing without leaning on position — references follow,
   * because they share the box.
   */
  private disambiguate(): void {
    const taken = new Set<string>();
    const last_definition = new Map<string, Box>();
    for (const line of this.lines) {
      for (const b of line) taken.add(b.symbol);
      if (line.length > 1) last_definition.set(line[0].symbol, line[0]);
    }

    let n = 0;
    for (const line of this.lines) {
      const head = line[0];
      if (line.length === 1 || last_definition.get(head.symbol) === head) continue;
      let name;
      do { name = `${head.symbol}:s${n++}`; } while (taken.has(name));
      taken.add(name);
      head.symbol = name;
    }
  }

  /**
   * Split into what the module shares and what each of `roots` has to itself.
   *
   * A definition belongs to a root when that root is the only thing that
   * reaches it. Anything reached by two roots, or by a name outside them, stays
   * in `shared` — so concatenating `shared` with any one root's part gives back
   * every line `extract` would have kept for that root, and no line is ever in
   * two parts at once.
   *
   * What counts as "outside" is the module's own notion of an interface: a
   * symbolic name is something a reader can ask for, a numeric id is internal
   * scaffolding. Every named definition other than a root is therefore treated
   * as reachable, and every id is reachable only through the lines that use it.
   *
   * The point is evaluation order. A module whose expensive parts are named
   * roots evaluates all of them the moment it is read; partitioned, `shared`
   * can be read once and each root's part evaluated against it on its own — at
   * its own cost, at a time of the caller's choosing, and without repeating
   * whatever two roots have in common.
   *
   * One backwards pass suffices: a reference resolves to a definition above it,
   * so by the time a line is reached, every line that could claim it has been
   * seen. Shadowed definitions are renamed first: a part is read after all of
   * `shared`, so a name `shared` goes on to bind again would mean the later one
   * by then. That rewrites this module in place — the parts hand back its own
   * lines, so there was never a copy to rename instead.
   */
  partition(roots: string[]): { shared: DagModule; exclusive: Map<string, DagModule> } {
    this.disambiguate();

    // A definition's owner: the one root that reaches it, or SHARED for
    // everything else. `undefined` means nothing has reached it yet.
    const SHARED = Symbol('shared');
    const owner = new Map<Box, string | symbol>();
    const claim = (b: Box, by: string | symbol) => {
      const had = owner.get(b);
      owner.set(b, had === undefined || had === by ? by : SHARED);
    };

    // A name shadowed later on was only scaffolding for what came after, so a
    // root claims its last definition and the earlier ones are shared.
    const wanted = new Set(roots);
    const last_definition = new Map<string, number>();
    this.lines.forEach((line, i) => {
      if (line.length > 1 && is_symbol_name(line[0].symbol)) last_definition.set(line[0].symbol, i);
    });
    this.lines.forEach((line, i) => {
      const head = line[0];
      if (line.length === 1) return claim(head, SHARED); // a terminator is the module's
      if (!is_symbol_name(head.symbol)) return;
      const name = head.symbol;
      claim(head, wanted.has(name) && last_definition.get(name) === i ? name : SHARED);
    });

    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      const by = owner.get(line[0]);
      if (by === undefined) continue; // nothing reaches this definition
      for (let j = 1; j < line.length; j++) claim(line[j], by);
    }

    const shared = new DagModule();
    const exclusive = new Map<string, DagModule>();
    for (const name of roots) {
      if (!this.definition(name)) throw new Error(`unknown symbol: ${name}`);
      exclusive.set(name, new DagModule());
    }

    for (const line of this.lines) {
      const by = owner.get(line[0]);
      (typeof by === 'string' ? exclusive.get(by)! : shared).lines.push(line);
    }
    return { shared, exclusive };
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
  qualify(prefix: string): this {
    const last_definition = new Map<string, number>();
    this.lines.forEach((line, i) => {
      if (line.length === 2 && !is_label(line[0].symbol))
        last_definition.set(line[0].symbol, i);
    });

    let internal = 0;
    this.lines.forEach((line, i) => {
      const head = line[0].symbol;
      const qualifiable =
        (line.length === 2 || (line.length === 3 && /^[a-zA-Z_]/.test(head)))
        && !is_label(head);
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
   *
   * `as_written` keys it on the spelling instead: a node is shared only with
   * one written the same way, so a node built through one name stays apart
   * from an equal node built through another. The value is the same either
   * way; what differs is which name each node is attributed to, for a reader
   * that treats a reference by name as meaning something the id does not.
   */
  canonicalize({ as_written = false }: { as_written?: boolean } = {}): DagModule {
    const LEAF_ID = 0;
    const ids = new Map<Box, number>();
    for (const line of this.lines)
      for (const b of line)
        if (b.symbol === LEAF) ids.set(b, LEAF_ID);

    // What a definition bound to something this module does not define stands
    // for: that thing, by its own name.
    const imports = new Map<Box, Box>();

    let next_id = 1;
    const forks = new Map<string, number>();
    const named = new Set<string>();
    // What each name means as the lines written so far bind it, as `key` says.
    const aliased = new Map<string, string>();
    const out = new DagModule();

    // How a reference is written: the leaf always as △, then any name already
    // defined above, then the node's id. As written, a name comes first even
    // for the leaf: `:t` bound to △ is still written `:t`. A name is used only
    // while the last line binding it binds this node: a box can be spelled like
    // a name that, by then, means something else.
    const ref = (b: Box): string => {
      const id = ids.get(b);
      const by_name = is_symbol_name(b.symbol) && named.has(b.symbol) && aliased.get(b.symbol) === key(b);
      if (id === LEAF_ID && !(as_written && by_name)) return LEAF;
      if (by_name) return b.symbol;
      if (id !== undefined) return String(id);
      // Nothing here defines it, so only its own name can say what it is —
      // and not once a line above has bound that name to something else.
      const outside = imports.get(b) ?? b;
      if (aliased.has(outside.symbol) && aliased.get(outside.symbol) !== key(b))
        throw new Error(`${outside.symbol} refers past this module, but is bound above its use`);
      return outside.symbol;
    };
    // What a box means, and so what sharing is decided on. Unresolved
    // references — symbols this module does not define — can only be told
    // apart by name.
    const key = (b: Box): string => {
      const id = ids.get(b);
      return id === undefined ? `?${(imports.get(b) ?? b).symbol}` : String(id);
    };

    // Write `name written`, which binds the name for every line below — unless
    // the lines above already bind it the same way. Only a name a two-word line
    // gave is then written in references: a label's three-word line marks its
    // node rather than naming it to the lines that use it.
    const bind = (name: string, meaning: string, written: string, nameable: boolean) => {
      const previous = aliased.get(name);
      if (previous === meaning) return; // already said
      if (previous === undefined) {
        if (nameable && is_symbol_name(name)) named.add(name);
      } else {
        // Redefined to something else — the name is no longer a stable
        // reference, so later lines must use ids.
        named.delete(name);
      }
      aliased.set(name, meaning);
      out.lines.push([box(name), box(written)]);
    };

    for (const line of this.lines) {
      if (line.length === 3) {
        const [head, left, right] = line;
        const fork_key = as_written ? `${ref(left)} ${ref(right)}` : `${key(left)} ${key(right)}`;
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
        // Labels name a node the caller wants to find again; keep them, as the
        // two-word line that says so.
        if (is_label(head.symbol)) {
          const id = ids.get(head)!;
          bind(head.symbol, key(head), id === LEAF_ID ? LEAF : String(id), false);
        }

      } else if (line.length === 2) {
        const [head, target] = line;
        const target_id = ids.get(target);
        if (target_id !== undefined) ids.set(head, target_id);
        else imports.set(head, imports.get(target) ?? target);
        // The target is written as the lines above have it: this one binds its
        // name only below itself, which matters when the two are spelled alike.
        bind(head.symbol, key(head), ref(target), true);

      } else if (line.length === 1) {
        out.lines.push([box(ref(line[0]))]);
      }
    }

    return out;
  }
}
