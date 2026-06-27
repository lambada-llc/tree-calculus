'''
Tests for the tree-calculus core (`tree_calculus`) and the single-step reducer
(`stepper`). Run with:

    python -m unittest test_tree_calculus -v
'''

import os
import sys
import unittest

from tree_calculus import reduce, parse_term, format_term
from stepper import step, trace_sampled

sys.setrecursionlimit(1_000_000)


def chain_length(t):
    '''Length of a unary stem-chain △ (△ (… △)): the number of stems before the
    terminating leaf. The `size` program encodes node counts this way. Raises
    ValueError if `t` is not a pure stem-chain (does not bottom out in a leaf).'''
    n = 0
    while isinstance(t, tuple) and len(t) == 1:
        n += 1; t = t[0]
    if t != ():
        raise ValueError(f'not a stem-chain: terminates in {format_term(t)!r}')
    return n


def node_count(t):
    '''Total number of △ nodes in a tree.'''
    return 1 + sum(node_count(child) for child in t)


def to_dot(t, name='tree'):
    '''Render a tree as Graphviz DOT. Every node is a △ (drawn as a small filled
    circle, matching the repo's diagrams); the tuple's elements hang below it,
    left to right, so a stem has one child and a fork two.'''
    lines = [
        f'digraph {name} {{',
        '  ordering=out;  // keep child order: distinguishes a fork\'s left/right',
        '  node [shape=circle, style=filled, fillcolor=black, '
        'fixedsize=true, width=0.2, label=""];',
        '  edge [dir=none];',
    ]
    counter = 0
    def visit(node):
        nonlocal counter
        me = counter; counter += 1
        lines.append(f'  n{me};')
        for child in node:
            lines.append(f'  n{me} -> n{visit(child)};')
        return me
    visit(t)
    lines.append('}')
    return '\n'.join(lines) + '\n'


# A "size" program: applied to a tree it returns that tree's node count as a
# unary stem-chain (so `size` of △ △ △ is △ (△ (△ △)) = 3). Embedded verbatim in
# △ notation and parsed via `parse_term`.
SIZE_EXPR = "△ (△ (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △))) (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △)))))))) (△ (△ (△ (△ (△ △ △)) (△ (△ (△ (△ △)) △)))) (△ (△ (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △)) △))) (△ △)))) (△ (△ (△ △ (△ (△ (△ △ △)) △))) (△ (△ (△ △ (△ (△ (△ △ (△ (△ (△ △ △)) △))) (△ △)))))))) (△ (△ (△ △ (△ △))))))))) (△ (△ (△ (△ (△ △ (△ (△ (△ (△ (△ (△ (△ △)) △)) (△ △)))))) (△ △)))))))) (△ △ △)"
size = parse_term(SIZE_EXPR)


class TreeCalculusTests(unittest.TestCase):
    def test_term_roundtrip(self):
        # Formatting then re-parsing is the identity on trees ...
        leaf, stem, fork = (), ((),), ((), ())
        for t in [leaf, stem, fork, (stem, leaf), size]:
            self.assertEqual(parse_term(format_term(t)), t)
        # ... and canonical notation prints back exactly.
        self.assertEqual(format_term(parse_term('△ (△ △) △')), '△ (△ △) △')

    def test_step_agrees_with_reduce(self):
        leaf, stem, fork = (), ((),), ((), ())
        vals = [leaf, stem, fork, (stem,), (stem, leaf), (fork, leaf)]
        for f in vals:
            for a in vals:
                t = f + (a,)                      # f applied to a (unreduced)
                stepped = t
                while (s := step(stepped)) is not None:
                    stepped = s
                self.assertEqual(stepped, reduce(t))

    def test_trace_size_small(self):
        # size of △ △ △ (a single fork, 3 nodes) is 3 = △ (△ (△ △)).
        print('\n# trace: size (△ △ △)')
        result = trace_sampled(size + (parse_term('△ △ △'),))
        self.assertEqual(format_term(result), '△ (△ (△ △))')
        self.assertEqual(chain_length(result), 3)

    def test_trace_size_size_chain_125(self):
        # size applied to itself returns the size program's own node count as a
        # unary stem-chain; that chain has length 125.
        print('\n# trace: size size')
        result = trace_sampled(size + (size,))
        self.assertEqual(chain_length(result), 125)

    def test_emit_size_size_dot(self):
        # Emit the (unreduced) term `size size` as a Graphviz DOT file next to
        # this test, e.g. `dot -Tsvg size_size.dot -o size_size.svg`.
        term = size + (size,)
        dot = to_dot(term, name='size_size')
        path = os.path.join(os.path.dirname(__file__), 'size_size.dot')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(dot)
        print(f'\n# wrote {path} ({node_count(term)} nodes)')
        # The DOT is a tree: one node per △, and exactly (nodes - 1) edges.
        nodes = node_count(term)
        self.assertEqual(dot.count(' -> '), nodes - 1)
        self.assertTrue(dot.startswith('digraph size_size {'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
