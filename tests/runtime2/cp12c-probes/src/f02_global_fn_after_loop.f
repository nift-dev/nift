a := [1,2,3]
fn(h()) { return g() }
fn(g()) { a = [9]
  return 0 }
for (x : a) { h(); print(x) }
print("done")
