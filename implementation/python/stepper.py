'''
A single-step reducer for tree calculus, on top of the representation in
`tree_calculus` (a tree is △ applied to its elements; applying one more argument
appends it to the tuple). Where `tree_calculus.reduce` runs a term to its normal
form in one go, `step` performs exactly one rewrite, so a reduction can be
observed term by term -- see `trace` / `trace_sampled`.
'''

from tree_calculus import format_term


# --- single-step reduction (normal order, leftmost-outermost) ---
# `step` returns the next term, or None if `t` is already a normal form. It
# mirrors `apply` rule by rule, but stops after a single rewrite.

def step(t):
    if len(t) >= 3:
        a, b, c, *w = t
        w = tuple(w)
        if len(a) >= 3:                 # function not a value yet: reduce it
            return (step(a),) + t[1:]
        if a == ():                     # rule 1: △ △ b c -> b
            return b + w
        if len(a) == 1:                 # rule 2: △ (△ x) b c -> (x c) (b c)
            (x,) = a
            return x + (c, b + (c,)) + w
        x, y = a                        # rule 3: △ (△ x y) b c, by shape of c
        if len(c) >= 3:                 # argument not a value yet: reduce it
            return (a, b, step(c)) + w
        if c == ():                     # 3a: c = △     -> x
            return x + w
        if len(c) == 1:                 # 3b: c = △ u   -> (y u)
            (u,) = c
            return y + (u,) + w
        u, v = c                        # 3c: c = △ u v -> (b u v)
        return b + (u, v) + w
    for i, e in enumerate(t):           # a value: reduce the leftmost child that can
        s = step(e)
        if s is not None:
            return t[:i] + (s,) + t[i+1:]
    return None

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
