#!/usr/bin/env python3
"""Marginal callgrind instructions per op for a given nift binary."""
import os, re, subprocess, sys, tempfile, json
BIN=sys.argv[1]; names=sys.argv[2:]
N1,N2=2000,4000
W={
 "loop":"i := 0\nwhile(i < {N}) {{ i += 1 }}\nprint(i)\n",
 "call":"fn(add(a, b)) {{ return a + b }}\ni := 0\ns := 0\nwhile(i < {N}) {{ s = add(s, i); i += 1 }}\nprint(s)\n",
 "call_top":"fn(add(a, b)) {{ return a + b }}\ns := 0\n{BODY}print(s)\n",
 "fib_top":"fn(fib(n)) {{ if(n < 2) {{ return n }} return fib(n - 1) + fib(n - 2) }}\nprint(fib({K}))\n",
}
def ir(src):
    d=tempfile.mkdtemp(); p=os.path.join(d,"t.f"); open(p,"w").write(src); out=os.path.join(d,"cg")
    r=subprocess.run(["valgrind","--tool=callgrind",f"--callgrind-out-file={out}",BIN,p],capture_output=True,text=True)
    m=re.search(r"Collected : (\d+)",r.stderr); return int(m.group(1)), r.stdout.strip()[-30:]
res={}
for n in names:
    if n=="call_top":
        a=ir(W[n].format(BODY="s = add(s, 1)\n"*200)); b=ir(W[n].format(BODY="s = add(s, 1)\n"*400)); res[n]=((b[0]-a[0])/200, b[1])
    elif n=="fib_top":
        a=ir(W[n].format(K=10)); b=ir(W[n].format(K=14)); res[n]=((b[0]-a[0])/(1219-177), b[1])  # calls: fib(14)=1219 calls, fib(10)=177
    else:
        a=ir(W[n].format(N=N1)); b=ir(W[n].format(N=N2)); res[n]=((b[0]-a[0])/(N2-N1), b[1])
print(json.dumps({k:[round(v[0]),v[1]] for k,v in res.items()}))
