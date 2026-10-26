fn[async](f(n)) { sleep(n); return n }
a := f(400)
b := f(10)
print(a.done())
print(a.status())
print(await b)
print(await a)
print(a.done())
print(a.status())
fn[async](bad()) { return 1 / 0 }
c := bad()
sleep(300)
print(c.status())
print(c.done())
