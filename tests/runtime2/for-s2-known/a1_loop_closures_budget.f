fs := []
for (e : range(100000)) { fs.push(() => 1) }
print(fs.length())
