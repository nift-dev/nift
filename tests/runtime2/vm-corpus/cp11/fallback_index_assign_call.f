fn(k()) { print("k"); return 0 }
fn(v()) { print("v"); return 5 }
a := [1]
a[k()] = v()
print(a[0])
