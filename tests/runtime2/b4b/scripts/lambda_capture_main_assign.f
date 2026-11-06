// U1 (found in the B4b F1 round; exit 70 on B4a 0df0df6c and e293105e): a lambda is walked
// before a later write grows the binding it captures (a top-level assignment after the lambda's walk);
// it is walked again and its trim is checked (the same error as legacy).
fn(t()) { return [1] }
fn(q()) { return t() }
xs := [" s "]
g := () => { return xs[0].trim() }
xs = q()
print(g())
