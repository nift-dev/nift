fn[async](f(n)) { return n }
i := 0
s := 0
while(i < 3) { s += await f(i); i += 1 }
print(s)
for(v : [1, 2]) { print(await f(v)) }
