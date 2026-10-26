fn[async](f(i)) { sleep(10); return i }
hs := []
i := 0
while(i < 200) { hs.push(f(i)); i += 1 }
s := 0
for(h : hs) { s += await h }
print(s)
