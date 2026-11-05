// B4: the same in a function called from a `for` loop (legacy's prepared evaluator).
fn(f(v)) { print(v.to_int()) }
print("before")
for (x : [2.5]) { f(x) }
