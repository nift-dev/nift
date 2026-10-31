// D-NEXT-refsem #1 (D4): returns share the instance.
struct(P) { v := 0 }
fn(g(x)) { return x }
fn(h(a)) { a.v = 2; return a }
p := P()
q := g(p)
q.v = 3
print(p.v)
r := h(p)
print(same(p, r))
print(r.v)
