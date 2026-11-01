// D-NEXT-refsem #1 (D4): a method through an alias mutates the instance.
struct(P) { v := 0; fn(bump()) { v += 1; return v } }
p := P()
q := p
q.bump()
q.bump()
print(p.v)
