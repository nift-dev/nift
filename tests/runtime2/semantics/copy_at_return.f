keep := [1]
fn(get()) { return keep }
r := get()
r.push(5)
print(keep.stringify())
print(r.stringify())
