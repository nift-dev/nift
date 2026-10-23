a := [1,2,3]
fn(shrink()) { a = [9] }
for (x : a) { shrink(); print("s") }
print("done")
