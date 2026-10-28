base := 10
c := async (x) => { base = base + 1; return base + x }
p := c(5)
base = 100
print(await p)
print(base)
