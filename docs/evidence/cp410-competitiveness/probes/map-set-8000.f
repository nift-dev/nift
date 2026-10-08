m := map(); s := set()

i := 0
while(i < 8000) { m.set(i,i+1); s.add(i); i += 1 }
print(m.size())

