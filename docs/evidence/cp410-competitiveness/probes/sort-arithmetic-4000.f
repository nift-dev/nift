a := []
i := 0
while(i < 4000) { a.push((i*7919) % 4000); i += 1 }

b := a.sort_by(x => x + 1)
print(b.size())

