shared := 0
fn[async](bump()) { shared += 1; shared += 1; return shared }
fn[async](peek()) { sleep(2); return shared }
a := bump()
b := peek()
shared = 10
print(await a)
print(await b)
print(shared)
c := peek()
print(await c)
