nest := [[1], [2]]
fn[async](grow_inner(xs)) { inner := xs[0]; inner.push(5); return inner.size() }
print(await grow_inner(nest))
print(nest[0].size())
