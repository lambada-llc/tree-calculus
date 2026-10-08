#!/usr/bin/env python3
"""mkin.py <cmdlog dir> <out file> <pid-seq|pid-a:b> ...  — concatenate logged commands into a runner stdin file."""
import sys, os
d, outp = sys.argv[1:3]
buf = b''
def one(s):
    global buf
    base = f'{d}/{s}'
    cmd = open(base + '.cmd', 'rb').read()
    if cmd.startswith(b'load '):
        buf += b'load ' + os.path.abspath(base + '.module').encode() + b'\n'
    else:
        payload = open(base + '.payload', 'rb').read()
        head = cmd.rsplit(b' ', 1)[0]
        buf += head + b' ' + str(len(payload)).encode() + b'\n' + payload
for a in sys.argv[3:]:
    pid, r = a.split('-')
    if ':' in r:
        lo, hi = map(int, r.split(':'))
        for i in range(lo, hi + 1):
            if os.path.exists(f'{d}/{pid}-{i}.cmd'): one(f'{pid}-{i}')
    else: one(a)
open(outp, 'wb').write(buf)
