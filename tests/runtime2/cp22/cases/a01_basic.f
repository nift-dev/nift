fn[async](add(a, b)) { return a + b }
base := 10
closure := async (x) => { return base + x }
a := add(2, 3)
b := closure(5)
base = 100
print(type(a))
print(await a)
print(await add(4, 5))
print(await b)
print(a.done())
print(a.status())
