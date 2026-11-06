// U1 (found in the B4b F1 round; exit 70 on B4a 0df0df6c and e293105e): a lambda is walked
// before a later write grows the binding it captures (an object member read through the captured binding);
// it is walked again and its trim is checked (the same error as legacy).
fn(t()) { return [1] }
fn(q()) { return t() }
o := {"a": " s "}
g := () => { return o.a.trim() }
fn(t2()) { return {"a": 1} }
fn(q2()) { return t2() }
o = q2()
print(g())
