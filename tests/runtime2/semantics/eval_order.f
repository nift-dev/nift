log := []
fn(t(v)) { log.push(v); return v }
r := t(1) + t(2) * t(3)
print(r)
a := [t(4), t(5)]
o := {"x": t(6), "y": t(7)}
fn(three(p, q, s)) { return p }
z := three(t(8), t(9), t(10))
print(log.stringify())
