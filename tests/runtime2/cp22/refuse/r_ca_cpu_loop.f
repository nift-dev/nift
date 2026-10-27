fn[async](work(n)) { i := 0; x := 0; while(i < n) { x = (x * 31 + i) % 1000003; i += 1 }; return x }
a := work(200000)
b := work(200000)
print((await a) + (await b))
