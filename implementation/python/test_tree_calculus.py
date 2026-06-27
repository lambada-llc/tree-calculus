'''
Tests for the tree-calculus core (`tree_calculus`) and the single-step reducer
(`stepper`). Run with:

    python -m unittest test_tree_calculus -v
'''

import sys
import unittest

from tree_calculus import reduce, parse_term, format_term
from stepper import step, trace_sampled

sys.setrecursionlimit(1_000_000)


def chain_length(t):
    '''Length of a unary stem-chain △ (△ (… △)): the number of stems before the
    terminating leaf. The `size` program encodes node counts this way.'''
    n = 0
    while isinstance(t, tuple) and len(t) == 1:
        n += 1; t = t[0]
    return n


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


if __name__ == '__main__':
    unittest.main(verbosity=2)
