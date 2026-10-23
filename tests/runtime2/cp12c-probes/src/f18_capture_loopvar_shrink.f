a := [1,2,3]
fs := []
for (x : a) { a = [9]; fs.push(() => x) }
print(fs.length())
