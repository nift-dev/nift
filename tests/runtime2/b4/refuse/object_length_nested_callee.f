// B4: the same through a second function (the callee of a callee of a `for` loop).
fn(g(v)) { return v.length() }
fn(f(v)) { return g(v) }
print("before")
for (i : [1]) { print(f({"a": 1, "b": 2})) }
