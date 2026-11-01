fn[async](add(a, b)) { return a + b }
print("ran")
f := add(1, 2)
g := f
h := add(1, 2)
print(same(f, g))
print(same(f, h))
print(await f)
print(await h)
