a := []
i := 0
while(i < 4000) { a.push(i); i += 1 }

b := a.map(x => x); print(b.size())

