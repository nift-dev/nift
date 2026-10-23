fs := []
x := 1
for (e : [1,2]) {
  if (true) {
    x := e + 10
    fs.push(() => x)
  }
}
for (f : fs) { print(f()) }
