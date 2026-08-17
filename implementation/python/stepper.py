'''
A single-step reducer for tree calculus, on top of the representation in
`tree_calculus` (a tree is △ applied to its elements; applying one more argument
appends it to the tuple). Where `tree_calculus.reduce` runs a term to its normal
form in one go, `step` performs exactly one rewrite, so a reduction can be
observed term by term -- see `trace` / `trace_sampled`.
'''

from tree_calculus import format_term, parse_ternary


# --- rule 2, with optional peeks (--peek) ---
# The three shortcuts that avoid duplicating c into (x c) (b c): x = K yields
# c outright (b c is never built), and an eliminator (K f) as x or b collapses
# its application to f. Each is a contraction of the plain sequence, so normal
# forms agree; only the step granularity changes.

peek = False

def _apply(f, a):
    '''f applied to a, collapsing an eliminator: (K f) a -> f.'''
    return f[1] if len(f) == 2 and f[0] == () else f + (a,)

def _rule2(x, b, c, w):
    if not peek:
        return x + (c, b + (c,)) + w
    if x == ((),):                  # x = K: -> c
        return c + w
    return _apply(x, c) + (_apply(b, c),) + w

# --- single-step reduction (normal order, leftmost-outermost) ---
# `step` returns the next term, or None if `t` is already a normal form. It
# mirrors `apply` rule by rule, but stops after a single rewrite.
# `_fire` is the head rewrite itself, for a head whose rule is determined.

def _fire(t):
    a, b, c, *w = t
    w = tuple(w)
    if a == ():                     # rule 1: △ △ b c -> b
        return b + w
    if len(a) == 1:                 # rule 2: △ (△ x) b c -> (x c) (b c)
        (x,) = a
        return _rule2(x, b, c, w)
    x, y = a                        # rule 3: △ (△ x y) b c, by shape of c
    if c == ():                     # 3a: c = △     -> x
        return x + w
    if len(c) == 1:                 # 3b: c = △ u   -> (y u)
        (u,) = c
        return y + (u,) + w
    u, v = c                        # 3c: c = △ u v -> (b u v)
    return b + (u, v) + w

# --fuse: compress transient size spikes into one recorded composite -- after
# a fire that grew the head node, keep firing there, up to `fuse` fires total,
# while it is still above its pre-fire size. Every recorded term is a term of
# the plain sequence.

fuse = 0

def _fireable(t):
    return len(t) >= 3 and len(t[0]) < 3 and (len(t[0]) < 2 or len(t[2]) < 3)

def _fire_chain(t):
    out = _fire(t)
    start, n = nodes(t), 1
    while n < fuse and nodes(out) > start and _fireable(out):
        out = _fire(out); n += 1
    return out

def step(t):
    if len(t) >= 3:
        a, c = t[0], t[2]
        if len(a) >= 3:                 # function not a value yet: reduce it
            return (step(a),) + t[1:]
        if len(a) == 2 and len(c) >= 3: # rule-3 argument not a value yet
            return t[:2] + (step(c),) + t[3:]
        return _fire_chain(t)
    for i, e in enumerate(t):           # a value: reduce the leftmost child that can
        s = step(e)
        if s is not None:
            return t[:i] + (s,) + t[i+1:]
    return None

# --- applicative order (leftmost-innermost, an eager evaluator's discipline) ---
# Arguments are values before a rule fires, so values are duplicated rather
# than pending work: terms stay near the sizes an eager evaluator would see.
# Terms that are only weakly normalizing (the __wn family) diverge under this
# order; the default root-first `step` reaches their normal form.

def step_applicative(t):
    for i, e in enumerate(t):
        s = step_applicative(e)
        if s is not None:
            return t[:i] + (s,) + t[i+1:]
    return _fire(t) if len(t) >= 3 else None

# --- shrink-eager order (--shrink-eager) ---
# Only rule 2 with a non-leaf argument can grow a term; every other fire
# shrinks it without duplicating anything, so shrink redexes fire first --
# biggest drop first -- and only when nothing shrinks does the root-first
# `step` pick the growing fire. Firing a shrink early can only make every
# later term smaller (a residual argument), so intermediates stay small. Same
# normal forms; not proven normalizing, so pair with --limit if in doubt.
# Picking growing fires by any global rule (leftmost in preorder, min-growth)
# instead of root-first's descent provably diverges on parts of the __wn
# family.

def _drop(t):
    '''Node-count decrease of firing a shrink redex (context-free).'''
    a, b, c = t[0], t[1], t[2]
    if a == ():                     # rule 1 drops c
        return 2 + nodes(c)
    if len(a) == 1:                 # rule 2 on a leaf duplicates only the leaf
        return 1
    x, y = a
    if c == ():                     # 3a drops y and b
        return 3 + nodes(y) + nodes(b)
    if len(c) == 1:                 # 3b drops x and b
        return 3 + nodes(x) + nodes(b)
    return 3 + nodes(x) + nodes(y)  # 3c drops x and y

def step_shrink_eager(t):
    best = None                     # (drop, path) of the best shrink redex
    def walk(t, path):
        nonlocal best
        if (len(t) >= 3 and len(t[0]) < 3 and (len(t[0]) < 2 or len(t[2]) < 3)
                and (len(t[0]) != 1 or t[2] == ())):
            d = _drop(t)
            if best is None or d > best[0]:
                best = (d, path)
        for i, e in enumerate(t):
            walk(e, path + (i,))
    walk(t, ())
    if best is None:                # nothing shrinks: root-first's pick
        return step(t)
    def fire_at(t, path):
        if not path:
            return _fire(t)
        i = path[0]
        return t[:i] + (fire_at(t[i], path[1:]),) + t[i+1:]
    return fire_at(t, best[1])

def count_steps(t):
    n = 0
    while (s := step(t)) is not None:
        t = s; n += 1
    return n

def trace(t):
    '''Print every term in the reduction sequence; return the normal form.'''
    while True:
        print(format_term(t))
        s = step(t)
        if s is None:
            return t
        t = s

def nodes(t):
    '''Number of nodes: a term is △ applied to its elements, so one node plus
    the nodes of each element.'''
    return 1 + sum(map(nodes, t))

def trace_sampled(t, max_states=10, width=200):
    '''Like `trace`, but for long reductions: print only every k-th term (so at
    most `max_states` are shown), truncating each to `width` characters with its
    full length annotated. The final normal form is always printed.'''
    k = max(1, (count_steps(t) + 1 + max_states - 1) // max_states)
    def show(t):
        s = format_term(t)
        return s if len(s) <= width else f'{s[:width]}… ({len(s)} chars)'
    i = 0
    while True:
        s = step(t)
        if s is None:
            print(show(t))
            return t
        if i % k == 0:
            print(show(t))
        t = s; i += 1


if __name__ == '__main__':
    # Trace a reduction: stdin is whitespace-separated ternary terms, applied
    # left-associatively; each step prints "<step> <nodes>" (--terms appends
    # the full term in △ notation).
    import argparse, sys
    sys.setrecursionlimit(1_000_000)
    p = argparse.ArgumentParser(description='Trace a reduction step by step.')
    p.add_argument('--terms', action='store_true', help='also print each term')
    p.add_argument('--limit', type=int, help='stop after this many steps')
    p.add_argument('--eager', action='store_true', help='applicative instead of root-first order')
    p.add_argument('--peek', action='store_true', help='rule-2 shortcuts that avoid duplicating the argument')
    p.add_argument('--shrink-eager', action='store_true', help='shrinking fires first; smallest intermediate terms')
    p.add_argument('--fuse', type=int, nargs='?', const=8, default=0,
                   help='compress transient spikes into composite steps, up to this many fires (implies --peek)')
    a = p.parse_args()
    peek = a.peek or a.fuse > 0
    fuse = a.fuse
    # a fresh name: rebinding `step` would turn step_shrink_eager's root-first
    # fallback into a call to itself
    step_fn = step_applicative if a.eager else step_shrink_eager if a.shrink_eager else step
    t = ()
    for i, line in enumerate(sys.stdin.read().split()):
        u = parse_ternary(line)
        t = u if i == 0 else t + (u,)
    i = 0
    while True:
        print(f'{i} {nodes(t)} {format_term(t)}' if a.terms else f'{i} {nodes(t)}')
        s = step_fn(t)
        if s is None or (a.limit is not None and i >= a.limit):
            break
        t = s; i += 1
