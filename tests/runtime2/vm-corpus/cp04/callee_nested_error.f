fn(inner(x)) { return x.nope() }
fn(outer(x)) { return inner(x) + 1 }
print(outer(1))
