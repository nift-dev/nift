fs := []
for (e : [1,2,3]) { f := () => e; fs.push(f) }
for (f : fs) { print(f()) }
