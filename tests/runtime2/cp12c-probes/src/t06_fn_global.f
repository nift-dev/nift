a := [1,2,3]
fn(shrink()) { a = [9] }
for (x : a) { print("s"); shrink(); print(x) }
print("done")
