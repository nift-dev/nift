fn[async](leaf(x)) { sleep(3); return x * 2 }
fn[async](waiter(p, k)) { v := await p; return v + k }
p := leaf(5)
a := waiter(p, 1)
b := waiter(p, 2)
print(await b)
print(await a)
print(await p)
