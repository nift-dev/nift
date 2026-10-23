a := [1,2,3]
once := true
for (x : a) { print(x); if (once) { a.push(4); once = false } }
