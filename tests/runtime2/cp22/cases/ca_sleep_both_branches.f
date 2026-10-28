fn[async](spin(n)) { i := 0; while (i < n) { i += 1; if (i % 2 == 0) { sleep(1) } else { sleep(2) } }; return i }
a := spin(4)
b := spin(3)
print((await a) + (await b))
