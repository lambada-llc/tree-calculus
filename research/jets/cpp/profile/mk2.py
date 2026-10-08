import sys
M = '/tmp/claude-0/-home-user/9ce76bb6-4f56-54a4-b5a6-f7971eb0dae5/scratchpad/jets/profile-dynamic/work/arb-cmdlog/1010-0.module'
kind, n, norm = sys.argv[1], int(sys.argv[2]), sys.argv[3]
L = ["c0 △ △"]   # unary n: stem chain over leaf
for i in range(1, n): L.append(f"c{i} △ c{i-1}")
c = f"c{n-1}"
L += ["s0 △ △"]
for i in range(20): L += [f"a{i} Snat.add s{i}", f"s{i+1} a{i} s{i}"]
L += [f"f Snat.add {c}"]   # f = add c (a partial application: a value)
if kind == 'native': L += [f"r f {c}", "r"]
else:
    L += ["qf Reflect.quote f", f"qx Reflect.quote {c}", "qa △ qf", "q qa qx"]
    if kind == 'quoted': L += [f"n0 Quoted.normalize_{norm} s20", "n n0 q", "n"]
    else: L += ["z △ q", "z"]
p = ('\n'.join(L) + '\n').encode()
sys.stdout.buffer.write(b'load ' + M.encode() + b'\nreduce dag ' + str(len(p)).encode() + b'\n' + p)
