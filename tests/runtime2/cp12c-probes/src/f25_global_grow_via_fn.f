a := [1,2,3]
fn(grow()) { a.push(4) }
for (x : a) { grow(); print(x) }
print(a.length())
