a := [1,2,3]
once := true
for (x : a) { print(x); if (once) { a = [1,2,3,4,5]; once = false } }
print(a.length())
