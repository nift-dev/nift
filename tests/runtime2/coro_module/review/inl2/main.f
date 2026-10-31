t := thread(target, 3)
fn(target(x)) { return x * 2 }
print(t.join())
