fn[async](f(i, s)) { t := s + i; throw error("x") }
s := ""
j := 0
while(j < 2000) { s += "a"; j += 1 }
hs := []
i := 0
while(i < 2000) { hs.push(f(i, s)); i += 1 }
for(h : hs) { try { await h } catch(e) { j += 1 } }
print("done")
