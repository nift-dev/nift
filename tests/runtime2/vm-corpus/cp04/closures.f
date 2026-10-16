fn(make(n)) { return (x) => x + n }
add2 := make(2)
print(add2(5))
counter := 0
fn(bump()) { counter += 1 }
bump()
bump()
print(counter)
