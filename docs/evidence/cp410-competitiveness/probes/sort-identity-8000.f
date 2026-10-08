a := []
i := 0
while(i < 8000) { a.push((i*7919) % 8000); i += 1 }

b := a.sort_by(x => x)
print(b.size())

