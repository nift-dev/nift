fn[async](sum(xs)) { s := 0; for (x : xs) { sleep(1); s += x }; return s }
a := sum([1, 2, 3])
b := sum([10, 20])
print((await a) + (await b))
