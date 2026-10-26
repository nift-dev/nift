fn[async](f(a)) { return a }
xs := [1, 2, 3].map((x) => x + 1)
print(await f(xs.size()))
