// D-NEXT-refsem #5: a thread receives its arguments as one alias-preserving copy (legacy refuses).
fn(w(a, b)) { a.set(1, 1); return b.size() }
m := map()
t := thread(w, m, m)
print(t.join())
print(m.size())
