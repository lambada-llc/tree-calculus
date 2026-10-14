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
    out, n = _fire(t), 1
    if n < fuse:
        start = nodes(t)
        while n < fuse and nodes(out) > start and _fireable(out):
            out = _fire(out); n += 1
    return out

def _root_path(t):
    '''Path to the redex `step` fires, or None for a normal form.'''
    if len(t) >= 3:
        a, c = t[0], t[2]
        if len(a) >= 3:                 # function not a value yet: reduce it
            return (0,) + _root_path(a)
        if len(a) == 2 and len(c) >= 3: # rule-3 argument not a value yet
            return (2,) + _root_path(c)
        return ()
    for i, e in enumerate(t):           # a value: reduce the leftmost child that can
        p = _root_path(e)
        if p is not None:
            return (i,) + p
    return None

def _fire_at(t, path, fire=_fire):
    if not path:
        return fire(t)
    i = path[0]
    return t[:i] + (_fire_at(t[i], path[1:], fire),) + t[i+1:]

def step(t):
    p = _root_path(t)
    return None if p is None else _fire_at(t, p, _fire_chain)

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

def _shrink_path(t):
    '''Path to the shrink redex that drops most (the first such), or None.'''
    best = None                     # (drop, path) of the best shrink redex
    def walk(t, path):
        nonlocal best
        if _fireable(t) and (len(t[0]) != 1 or t[2] == ()):
            d = _drop(t)
            if best is None or d > best[0]:
                best = (d, path)
        for i, e in enumerate(t):
            walk(e, path + (i,))
    walk(t, ())
    return best and best[1]

def step_shrink_eager(t):
    p = _shrink_path(t)
    if p is None:                   # nothing shrinks: root-first's pick
        return step(t)
    return _fire_at(t, p)

# --- shrink-pending order (--shrink-pending) ---
# shrink-eager with a different measure: not the term's size but the work
# still pending in it -- every node counted once per unreduced application it
# sits under. In △ e1 .. ek those are the k-2 applications of e3 .. ek, the
# one of ej covering the head and e1 .. ej; a node under several counts once
# for each, so a normal form is exactly a term of measure 0. The fire that
# lowers it most goes first; when none does, root-first's pick, as above.

def _pending(t, memo):
    '''(nodes, measure) of t, memoized by identity for one step (the memo
    holds t, so its id is not reused while the memo lives).'''
    r = memo.get(id(t))
    if r is None:
        n = m = 0
        for j, e in enumerate(t, 1):
            en, em = _pending(e, memo)
            n += en; m += em
            if j >= 3:              # the application of ej: head and e1 .. ej
                m += 1 + n
        r = memo[id(t)] = (1 + n, m, t)
    return r[:2]

def step_shrink_pending(t):
    memo, best = {}, None           # (change, path) of the best lowering fire
    def walk(t, path, above):       # above: pending applications over t
        nonlocal best
        if _fireable(t):
            n, m = _pending(t, memo)
            n2, m2 = _pending(_fire(t), memo)
            d = above * (n2 - n) + m2 - m
            if d < 0 and (best is None or d < best[0]):
                best = (d, path)
        for j, e in enumerate(t, 1):
            walk(e, path + (j - 1,), above + max(0, len(t) - max(j, 3) + 1))
    walk(t, (), 0)
    if best is None:                # nothing lowers it: root-first's pick
        return step(t)
    return _fire_at(t, best[1])

# --- cheap-args order (--cheap-args) ---
# shrink-eager, except that before rule 2 passes its argument c on, c is
# taken to normal form first when that is cheap: when root-first gets it
# there on its own within CHEAP steps per node. Laziness otherwise carries big
# pending arguments around -- often ones that evaluate to a single node --
# and that is most of the term's size along the way; evaluated in time, the
# term stays smaller for longer. An argument without a cheap normal form is
# passed on lazily as before: in the __wn family that includes the ones with
# no normal form at all, so the bounded trial is what keeps this from
# diverging there (it is not proven normalizing). Arguments inside c are
# decided the same way. Measured on every `size size` variant: peak and
# mean never above shrink-eager's, the mean down by a third on average.
# CHEAP = 3 is the best of 1 .. 10 overall; 2 to 5 all stay at or below.
# A large budget (--cheap-args 100) suits a program whose loop arguments
# always have a normal form: it brings the loop back to its smallest state,
# both arguments evaluated, once per iteration (Snat.size__78's area drops to
# a tenth of CHEAP = 3's); on a loop with a second phase it costs instead.
# GROW keeps a large budget affordable: an argument without a normal form
# usually grows, and its trial ends there instead of running out the budget.
# At CHEAP = 3 it decides nothing differently on any size-of-size variant.

CHEAP = 3
GROW = 16   # a trial that grows its argument past GROW times its size fails
_dist = {}  # hash(term) -> (root-first steps to its normal form, largest size on
            # the way), or (-n, None): at least n steps
_grows = set()  # hashes of arguments whose trial grew past GROW times their size

def _cheap(c, budget):
    '''Does root-first take c to normal form within `budget` steps per node,
    never growing it past GROW times its size? Every term a trial passes
    through keeps its distance, so the trials of a run share their work.
    Keyed by hash, so the terms themselves are not kept: a collision could
    only change which redex fires, never the normal form.'''
    n = nodes(c)
    cap, lim, h = budget * n, GROW * n, hash(c)
    if h in _grows:
        return False
    r = _dist.get(h)
    if r is None or (r[1] is None and -r[0] <= cap):
        seen, u, size = [], c, n
        while True:
            k = hash(u)
            r = _dist.get(k)
            if r is not None and r[1] is not None:
                break               # joins a known way to the normal form
            s = step(u)
            if s is None:
                r = _dist[k] = (0, size)
                break
            seen.append((k, size))
            u, size = s, nodes(s)
            if len(seen) > cap or size > lim:
                if size > lim:
                    _grows.add(h)
                for i, (k, _) in enumerate(seen):
                    old = _dist.get(k)
                    _dist[k] = (min(old[0] if old else 0, i - len(seen)), None)
                return False
        d, peak = r
        for k, size in reversed(seen):
            d, peak = d + 1, max(peak, size)
            _dist[k] = (d, peak)
        r = _dist[h]
    return r[1] is not None and r[0] <= cap and r[1] <= lim

def _arg_paths(t, budget):
    '''Root-first's redex, then -- while that is a rule 2 whose argument is
    cheap within `budget` -- the argument's own, and so on inwards.'''
    p = _root_path(t)
    while p is not None:
        yield p
        r = _subterm(t, p)
        if len(r[0]) != 1 or (q := _root_path(r[2])) is None or not _cheap(r[2], budget):
            return
        p += (2,) + q

def step_cheap_args(t):
    p = _shrink_path(t)
    if p is None:
        for p in _arg_paths(t, CHEAP):
            pass                    # the innermost: arguments first
    return None if p is None else _fire_at(t, p)

# --- lookahead order (--lookahead H) ---
# cheap-args deciding by trial instead of by budget: when nothing shrinks,
# the options are root-first's redex and the ones inside the arguments its
# rule 2s would pass on (when those normalize within LOOK_BUDGET steps per
# node); each is scored by the area under the next H steps of cheap-args,
# and the smallest wins (the outermost on a tie). On a loop with a second
# phase it beats every fixed budget (Snat.size__118: a fifth less area than
# cheap-args at H = 256), but it is slow: each step costs H steps per option.

LOOK_BUDGET = 20
lookahead = 256

def step_lookahead(t):
    p = _shrink_path(t)
    if p is not None:
        return _fire_at(t, p)
    best = None
    for p in _arg_paths(t, LOOK_BUDGET):
        u = _fire_at(t, p)
        area = nodes(u)
        for _ in range(lookahead):
            if (u := step_cheap_args(u)) is None:
                break
            area += nodes(u)
        if best is None or area < best[0]:
            best = (area, p)
    return None if best is None else _fire_at(t, best[1])

def _subterm(t, path):
    for i in path:
        t = t[i]
    return t

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
    p.add_argument('--shrink-pending', action='store_true', help='fires lowering the pending work first')
    p.add_argument('--cheap-args', type=int, nargs='?', const=CHEAP, metavar='N',
                   help='shrink-eager, normalizing arguments before rule 2 passes them on when that takes '
                        'root-first at most N steps per node (default %d)' % CHEAP)
    p.add_argument('--lookahead', type=int, nargs='?', const=lookahead, metavar='H',
                   help='cheap-args choosing each argument to evaluate by the area of the next H steps '
                        '(default %d); slow' % lookahead)
    p.add_argument('--fuse', type=int, nargs='?', const=8, default=0,
                   help='compress transient spikes into composite steps, up to this many fires (implies --peek)')
    a = p.parse_args()
    peek = a.peek or a.fuse > 0
    fuse = a.fuse
    CHEAP = a.cheap_args or CHEAP
    lookahead = a.lookahead or lookahead
    # a fresh name: rebinding `step` would turn the shrink orders' root-first
    # fallback into a call to itself
    step_fn = (step_applicative if a.eager else step_shrink_eager if a.shrink_eager
               else step_shrink_pending if a.shrink_pending else step_cheap_args if a.cheap_args is not None
               else step_lookahead if a.lookahead is not None else step)
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
