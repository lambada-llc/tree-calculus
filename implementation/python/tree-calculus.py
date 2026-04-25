'''
TC        | Python
----------+----------
△        | ()
△ a      | (a,)
△ a b    | (a, b)
△ a b c  | (a, b, c)
'''

# Example usage of "apply": negating booleans
# _false = ()
# _true = ((),)
# _not = ((((),),((),())),())
# print(apply(_not, _false))  # ((),)
# print(apply(_not, _true))   # ()
def apply(a, b):
    match a:
        case ():
            return (b,)
        case (x,):
            return (x, b)
        case ((), y):
            return y
        case ((x,), y):
            return apply(apply(x, b), apply(y, b))
        case ((w, x), y):
            match b:
                case ():
                    return w
                case (u,):
                    return apply(x, u)
                case (u, v):
                    return apply(apply(y, u), v)

def reduce(t):
    t = tuple(map(reduce, t))
    while len(t) > 2:
        x, y, z, *w = t
        t = (*apply((x, y), z), *w)
    return t


def parse_ternary(s):
    it = iter(s)
    def parse():
        match next(it):
            case '0': return ()
            case '1': return (parse(),)
            case '2': return (parse(), parse())
    return parse()

def format_ternary(t):
    match t:
        case ():        return '0'
        case (x,):      return '1' + format_ternary(x)
        case (x, y):    return '2' + format_ternary(x) + format_ternary(y)

if __name__ == '__main__':
    import sys
    prog_line, arg_line = sys.stdin.read().splitlines()[:2]
    prog = parse_ternary(prog_line)
    arg  = parse_ternary(arg_line)
    print(format_ternary(apply(prog, arg)))
