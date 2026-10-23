a := [1,2]
fn(reset()) { a = [5,6] }
for (x : a) { reset(); print(x) }
print("done")
