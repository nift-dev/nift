a := [1,2,3]
go := false
fn(shrink()) { a = [9] }
for (x : a) { shrink(); if (go) { print(x) }; print("s") }
print("done")
