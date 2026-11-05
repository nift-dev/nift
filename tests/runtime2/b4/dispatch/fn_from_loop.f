// B4: a function called from loop code and from top level (legacy's two evaluators agree here).
fn(up(v)) { print(v.to_upper()) }
print("before")
for (i : [1, 2]) { up("x" + i) }
up("y")
up(true)
