f := (x) => x ? "yes" : "no"
print(f(true))
print(f(false))
g := [(x) => x ? "yes" : "no"]
print(g[0](false))
