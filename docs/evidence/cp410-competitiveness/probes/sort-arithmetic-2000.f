a := []
i := 0
while(i < 2000) { a.push((i*7919) % 2000); i += 1 }

b := a.sort_by(x => x + 1)
print(b.size())

