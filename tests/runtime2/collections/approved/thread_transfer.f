fn(w(m)) { m.set("w", 1); return m }
m := map(); m.set(1, [1, 2])
t := thread(w, m)
r := t.join()
print(m.stringify())
print(r.stringify())
fn(mk(n)) { s := set(); i := 0; while (i < n) { s.add(i); i += 1 }; return s }
u := thread(mk, 20)
s := u.join()
print(s.size())
print(s.contains(19))
print(s.contains(20))
