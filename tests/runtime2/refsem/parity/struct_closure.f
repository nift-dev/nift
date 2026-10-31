// D-NEXT-refsem #1 (D4): closures see the shared instance.
struct(P) { v := 0 }
p := P()
f := () => p.v
p.v = 2
print(f())
g := () => p
q := g()
q.v = 3
print(p.v)
