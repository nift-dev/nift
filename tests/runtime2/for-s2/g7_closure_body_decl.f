fs := []
for (e : [1,2,3]) { v := e * 10; f := () => v; fs.push(f) }
for (f : fs) { print(f()) }
