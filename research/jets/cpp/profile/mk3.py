import sys
M = '/tmp/claude-0/-home-user/9ce76bb6-4f56-54a4-b5a6-f7971eb0dae5/scratchpad/jets/profile-dynamic/work/arb-cmdlog/1010-0.module'
n, k, norm = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
L = ["c0 △ △"]
for i in range(1, n): L.append(f"c{i} △ c{i-1}")
c = f"c{n-1}"
L += ["s0 △ △"]
for i in range(k): L += [f"a{i} Snat.add s{i}", f"s{i+1} a{i} s{i}"]
L += [f"f Snat.add {c}", "qf Reflect.quote f", f"qx Reflect.quote {c}", "qa △ qf", "q qa qx",
      f"n0 Quoted.normalize_{norm} s{k}", "n n0 q", "z △ n", "isnone z △", "isnone"]
p = ('\n'.join(L) + '\n').encode()
sys.stdout.buffer.write(b'load ' + M.encode() + b'\nreduce dag ' + str(len(p)).encode() + b'\n' + p)
