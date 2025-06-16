'''
TC        | Python
----------+----------
△        | ()
△ a      | (a,)
△ a b    | (a, b)
△ a b c  | (a, b, c)
'''


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


# Example: negating booleans
_false = ()
_true = ((),)
_not = ((((),),((),())),())

print(apply(_not, _false))  # ((),)
print(apply(_not, _true))   # ()
