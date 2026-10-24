fs := []
for (x : [1,2,3]) { fs.push(() => loop.index) }
for (f : fs) { print(f()) }
