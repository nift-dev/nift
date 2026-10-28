h := async (x) => x * 2
print(await h(4))
k := async (x) => { return x + 1 }
m := k
print(await m(1))
