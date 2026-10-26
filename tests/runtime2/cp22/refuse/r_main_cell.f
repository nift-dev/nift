fn[async](f(a)) { return a }
if (true) { n := 1; g := () => n; print(g()) }
print(await f(2))
