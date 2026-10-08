# minimal numpy stand-in for fit.py: arrays as lists, lstsq via normal equations
class _NP:
    def array(self, x): return x
    def min(self, a, axis=0): return [min(c) for c in zip(*a)]
np = _NP()
