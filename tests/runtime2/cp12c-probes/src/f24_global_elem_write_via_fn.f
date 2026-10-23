a := [1,2,3]
fn(setit()) { a[1] = 50 }
for (x : a) { setit(); print(x) }
print("done")
