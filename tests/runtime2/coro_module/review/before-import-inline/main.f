fn(w()) { return target(3) }
t := thread(w)
fn(target(x)) { return x * 2 }
print(t.join())
