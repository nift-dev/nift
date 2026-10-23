fs := []
for (x : [1,2,3]) { y := x * 10
  fs.push(() => x + y) }
for (f : fs) { print(f()) }
