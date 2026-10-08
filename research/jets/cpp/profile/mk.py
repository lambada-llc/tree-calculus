import sys
M = '/tmp/claude-0/-home-user/9ce76bb6-4f56-54a4-b5a6-f7971eb0dae5/scratchpad/jets/profile-dynamic/work/arb-cmdlog/1010-0.module'
def tree(depth):
    L = ["x1 △ △", "t0 x1 △"]  # t0 = △△△ (fork leaf leaf)
    for d in range(1, depth + 1):
        L += [f"u{d} △ t{d-1}", f"t{d} u{d} t{d-1}"]
    return L, f"t{depth}"
def budget(log2):
    L = ["s0 △ △"]
    for i in range(log2):
        L += [f"a{i} Snat.add s{i}", f"s{i+1} a{i} s{i}"]
    return L, f"s{log2}"
def payload(kind, fn, depth, log2=20):
    tl, t = tree(depth)
    if kind == 'native':
        L = tl + [f"r {fn} {t}", "r"]
    else:
        bl, b = budget(log2)
        L = tl + bl + [f"qf Reflect.quote {fn}", f"qx Reflect.quote {t}", "qa △ qf", "q qa qx"]
        if kind == 'quoted':
            L += [f"n0 Quoted.normalize_{sys.argv[4] if len(sys.argv)>4 else 'lazy'} {b}", "n n0 q", "n"]
        else:  # base: everything but the normalization
            L += ["z △ q", "z"]
    p = ('\n'.join(L) + '\n').encode()
    return b'load ' + M.encode() + b'\n' + b'reduce dag ' + str(len(p)).encode() + b'\n' + p
sys.stdout.buffer.write(payload(sys.argv[1], sys.argv[2], int(sys.argv[3])))
