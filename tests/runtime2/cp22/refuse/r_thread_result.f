fn(id(x)) { return x }
fn(mk()) { return thread(id, 3) }
h := thread(mk)
print(h.join().join())
